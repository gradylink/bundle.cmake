#ifndef BUNDLE_HPP
#define BUNDLE_HPP

#include "bundle_runtime.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef BUNDLE_NO_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define BUNDLE_NO_EXCEPTIONS 0
#else
#define BUNDLE_NO_EXCEPTIONS 1
#endif
#endif

#if BUNDLE_NO_EXCEPTIONS
#define BUNDLE_REPORT(ec, code, what)                                                                                                                                         \
  do {                                                                                                                                                                        \
    if (ec) {                                                                                                                                                                 \
      *(ec) = (code);                                                                                                                                                         \
    } else {                                                                                                                                                                  \
      (void)(what);                                                                                                                                                           \
      std::abort();                                                                                                                                                           \
    }                                                                                                                                                                         \
  } while (0)
#else
#define BUNDLE_REPORT(ec, code, what)                                                                                                                                         \
  do {                                                                                                                                                                        \
    if (ec) {                                                                                                                                                                 \
      *(ec) = (code);                                                                                                                                                         \
    } else {                                                                                                                                                                  \
      throw ::bundle::error((code), (what));                                                                                                                                  \
    }                                                                                                                                                                         \
  } while (0)
#endif

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
  explicit streambuf(const entry &e, int *ec = nullptr) {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    int rc = bundle_open(&e.raw(), &stream_);
    if (rc != BUNDLE_OK) {
      error_ = rc;
      BUNDLE_REPORT(ec, rc, "bundle::streambuf: bundle_open failed");
      return;
    }
    setg(buffer_, buffer_, buffer_);
  }

  ~streambuf() override { bundle_close(&stream_); }

  streambuf(const streambuf &) = delete;
  streambuf &operator=(const streambuf &) = delete;

  int error() const noexcept { return error_; }

protected:
  int_type underflow() override {
    if (gptr() < egptr()) {
      return traits_type::to_int_type(*gptr());
    }
    if (error_ != BUNDLE_OK) {
      return traits_type::eof();
    }

    size_t n = bundle_read(&stream_, buffer_, sizeof(buffer_));
    if (n == 0) {
      int status = bundle_stream_status(&stream_);
      if (status != BUNDLE_OK) {
        error_ = status;
#if !BUNDLE_NO_EXCEPTIONS
        throw ::bundle::error(status, "bundle::streambuf: bundle_read failed");
#endif
      }
      return traits_type::eof();
    }

    setg(buffer_, buffer_, buffer_ + n);
    return traits_type::to_int_type(*gptr());
  }

private:
  bundle_stream stream_{};
  char buffer_[65536];
  int error_ = BUNDLE_OK;
};

class istream : public std::istream {
public:
  explicit istream(const entry &e, int *ec = nullptr) : std::istream(nullptr), buf_(std::make_unique<streambuf>(e, ec)) { rdbuf(buf_.get()); }

  istream(const istream &) = delete;
  istream &operator=(const istream &) = delete;

  istream(istream &&other) noexcept : std::istream(std::move(other)), buf_(std::move(other.buf_)) { rdbuf(buf_.get()); }
  istream &operator=(istream &&other) noexcept {
    std::istream::operator=(std::move(other));
    buf_ = std::move(other.buf_);
    rdbuf(buf_.get());
    return *this;
  }

  /** Error recorded by the last failed read, or BUNDLE_OK. @sa streambuf::error */
  int error() const noexcept { return buf_->error(); }

private:
  std::unique_ptr<streambuf> buf_;
};

class archive {
public:
  explicit archive(bundle_archive raw) : raw_(raw) {}

  std::optional<entry> find(std::string_view path, int *ec = nullptr) const {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    bundle_entry e;
    std::string path_str(path);
    int rc = bundle_find(&raw_, path_str.c_str(), &e);
    if (rc == BUNDLE_ERROR_NOT_FOUND) {
      return std::nullopt;
    }
    if (rc != BUNDLE_OK) {
      BUNDLE_REPORT(ec, rc, "bundle::archive::find failed");
      return std::nullopt;
    }
    return entry(e);
  }

  long size(int *ec = nullptr) const {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    long n = bundle_entry_count(&raw_);
    if (n < 0) {
      BUNDLE_REPORT(ec, (int)n, "bundle::archive::size failed");
      return -1;
    }
    return n;
  }

  /**
   * Decompresses the entire entry into memory.
   * @sa bundle::load_text
   */
  std::vector<std::byte> load(const entry &e, int *ec = nullptr) const {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    std::vector<std::byte> buf(e.size());
    long n = bundle_read_all(&e.raw(), buf.data(), buf.size());
    if (n < 0) {
      BUNDLE_REPORT(ec, (int)n, "bundle::archive::load failed");
      return {};
    }
    return buf;
  }

  /**
   * Convenience for text assets: same as load(), but into a std::string.
   * @sa bundle::load
   */
  std::string load_text(const entry &e, int *ec = nullptr) const {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    std::string buf(e.size(), '\0');
    long n = bundle_read_all(&e.raw(), buf.data(), buf.size());
    if (n < 0) {
      BUNDLE_REPORT(ec, (int)n, "bundle::archive::load_text failed");
      return {};
    }
    return buf;
  }

  /** Streams the entry's decompressed content directly to a file. */
  void extract_to(const entry &e, const std::filesystem::path &destination, int *ec = nullptr) const {
    if (ec) {
      *ec = BUNDLE_OK;
    }
    std::ofstream out(destination, std::ios::binary);
    if (!out) {
      BUNDLE_REPORT(ec, BUNDLE_ERROR_IO, "bundle::archive::extract_to: failed to open " + destination.string());
      return;
    }

    bundle_stream stream;
    int rc = bundle_open(&e.raw(), &stream);
    if (rc != BUNDLE_OK) {
      BUNDLE_REPORT(ec, rc, "bundle::archive::extract_to: bundle_open failed");
      return;
    }

    char chunk[65536];
    size_t n;
    while ((n = bundle_read(&stream, chunk, sizeof(chunk))) > 0) {
      out.write(chunk, static_cast<std::streamsize>(n));
    }
    rc = bundle_stream_status(&stream);
    bundle_close(&stream);

    if (rc != BUNDLE_OK) {
      BUNDLE_REPORT(ec, rc, "bundle::archive::extract_to: bundle_read failed");
    }
  }

  istream open(const entry &e, int *ec = nullptr) const { return istream(e, ec); }

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

    /** Error recorded if iteration stopped early, or BUNDLE_OK. */
    int error() const noexcept { return error_; }

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
        error_ = rc;
#if !BUNDLE_NO_EXCEPTIONS
        throw ::bundle::error(rc, "bundle::archive::iterator: bundle_iter_next failed");
#endif
      }
    }

    bundle_iterator it_{};
    entry current_{};
    bool done_ = true;
    int error_ = BUNDLE_OK;
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

#undef BUNDLE_REPORT

#endif
