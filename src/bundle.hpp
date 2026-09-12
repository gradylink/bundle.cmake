#ifndef BUNDLE_HPP
#define BUNDLE_HPP

#include "bundle_runtime.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bundle {

class error : public std::runtime_error {
public:
  error(int code, std::string_view what) : std::runtime_error(std::string(what)), code_(code) {}

  int code() const noexcept { return code_; }

private:
  int code_;
};

class entry {
public:
  entry() = default;
  explicit entry(const bundle_entry &raw) : raw_(raw) {}

  std::string name() const {
    std::string result(raw_.name_length, '\0');
    if (raw_.name_length > 0) {
      std::string scratch(raw_.name_length + 1, '\0');
      bundle_entry_name(&raw_, scratch.data(), scratch.size());
      result.assign(scratch, 0, raw_.name_length);
    }
    return result;
  }

  size_t size() const { return raw_.uncompressed_size; }
  unsigned short method() const { return raw_.method; }
  const bundle_entry &raw() const { return raw_; }

private:
  bundle_entry raw_{};
};

class streambuf : public std::streambuf {
public:
  explicit streambuf(const entry &e) {
    int rc = bundle_open(&e.raw(), &stream_);
    if (rc != BUNDLE_OK) {
      throw error(rc, "bundle::streambuf: bundle_open failed");
    }
    setg(buffer_, buffer_, buffer_);
  }

  ~streambuf() override { bundle_close(&stream_); }

  streambuf(const streambuf &) = delete;
  streambuf &operator=(const streambuf &) = delete;

protected:
  int_type underflow() override {
    if (gptr() < egptr()) {
      return traits_type::to_int_type(*gptr());
    }

    size_t n = bundle_read(&stream_, buffer_, sizeof(buffer_));
    if (n == 0) {
      int status = bundle_stream_status(&stream_);
      if (status != BUNDLE_OK) {
        throw error(status, "bundle::streambuf: bundle_read failed");
      }
      return traits_type::eof();
    }

    setg(buffer_, buffer_, buffer_ + n);
    return traits_type::to_int_type(*gptr());
  }

private:
  bundle_stream stream_{};
  char buffer_[65536];
};

class istream : public std::istream {
public:
  explicit istream(const entry &e) : std::istream(nullptr), buf_(std::make_unique<streambuf>(e)) { rdbuf(buf_.get()); }

  istream(const istream &) = delete;
  istream &operator=(const istream &) = delete;
  istream(istream &&) = default;
  istream &operator=(istream &&) = default;

private:
  std::unique_ptr<streambuf> buf_;
};

class archive {
public:
  explicit archive(bundle_archive raw) : raw_(raw) {}

  std::optional<entry> find(std::string_view path) const {
    bundle_entry e;
    std::string path_str(path);
    int rc = bundle_find(&raw_, path_str.c_str(), &e);
    if (rc == BUNDLE_ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    if (rc != BUNDLE_OK) {
      throw error(rc, "bundle::archive::find failed");
    }
    return entry(e);
  }

  long size() const {
    long n = bundle_entry_count(&raw_);
    if (n < 0) {
      throw error((int)n, "bundle::archive::size failed");
    }
    return n;
  }

  /**
   * Decompresses the entire entry into memory.
   * @sa bundle::load_text
   */
  std::vector<std::byte> load(const entry &e) const {
    std::vector<std::byte> buf(e.size());
    long n = bundle_read_all(&e.raw(), buf.data(), buf.size());
    if (n < 0) {
      throw error((int)n, "bundle::archive::load failed");
    }
    return buf;
  }

  /**
   * Convenience for text assets: same as load(), but into a std::string.
   * @sa bundle::load
   */
  std::string load_text(const entry &e) const {
    std::string buf(e.size(), '\0');
    long n = bundle_read_all(&e.raw(), buf.data(), buf.size());
    if (n < 0) {
      throw error((int)n, "bundle::archive::load_text failed");
    }
    return buf;
  }

  /** Streams the entry's decompressed content directly to a file. */
  void extract_to(const entry &e, const std::filesystem::path &destination) const {
    std::ofstream out(destination, std::ios::binary);
    if (!out) {
      throw std::runtime_error("bundle::archive::extract_to: failed to open " + destination.string());
    }

    bundle_stream stream;
    int rc = bundle_open(&e.raw(), &stream);
    if (rc != BUNDLE_OK) {
      throw error(rc, "bundle::archive::extract_to: bundle_open failed");
    }

    char chunk[65536];
    size_t n;
    while ((n = bundle_read(&stream, chunk, sizeof(chunk))) > 0) {
      out.write(chunk, static_cast<std::streamsize>(n));
    }
    rc = bundle_stream_status(&stream);
    bundle_close(&stream);

    if (rc != BUNDLE_OK) {
      throw error(rc, "bundle::archive::extract_to: bundle_read failed");
    }
  }

  istream open(const entry &e) const { return istream(e); }

  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const entry *;
    using reference = const entry &;

    iterator() = default;
    explicit iterator(const bundle_archive *archive_ptr) {
      if (bundle_iter_begin(archive_ptr, &it_) != BUNDLE_OK) {
        return;
      }
      done_ = false;
      advance();
    }

    reference operator*() const { return current_; }
    pointer operator->() const { return &current_; }

    iterator &operator++() {
      advance();
      return *this;
    }

    friend bool operator==(const iterator &a, const iterator &b) { return a.done_ == b.done_; }
    friend bool operator!=(const iterator &a, const iterator &b) { return !(a == b); }

  private:
    void advance() {
      bundle_entry e;
      int rc = bundle_iter_next(&it_, &e);
      if (rc == 1) {
        current_ = entry(e);
      } else if (rc == 0) {
        done_ = true;
      } else {
        done_ = true;
        throw error(rc, "bundle::archive::iterator: bundle_iter_next failed");
      }
    }

    bundle_iterator it_{};
    entry current_{};
    bool done_ = true;
  };

  iterator begin() const { return iterator(&raw_); }
  iterator end() const { return iterator(); }

  std::vector<entry> entries_with_prefix(std::string_view prefix) const {
    std::vector<entry> result;
    for (const auto &e : *this) {
      std::string name = e.name();
      if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
        result.push_back(e);
      }
    }
    return result;
  }

  const bundle_archive &raw() const { return raw_; }

private:
  bundle_archive raw_;
};

} // namespace bundle

#endif
