#include "bundle.hpp"
#include "bundle_poems.h"

#include <filesystem>
#include <iostream>
#include <sstream>

int main() {
  bundle::archive archive(bundle_poems_archive());

  std::cout << "entries: " << archive.size() << "\n";
  for (const auto &e : archive) {
    std::cout << "  - " << e.name() << " (" << e.size() << " bytes)\n";
  }

  auto entry = archive.find("assets/poem.txt");
  if (!entry) {
    std::cerr << "assets/poem.txt not found\n";
    return 1;
  }

  bundle::istream in = archive.open(*entry);
  std::string line;
  int line_count = 0;
  std::cout << "--- streamed line by line ---\n";
  while (std::getline(in, line)) {
    std::cout << "[" << ++line_count << "] " << line << "\n";
  }

  std::string whole = archive.load_text(*entry);
  if (whole.size() != entry->size()) {
    std::cerr << "load_text size mismatch\n";
    return 1;
  }

  auto out_path = std::filesystem::temp_directory_path() / "bundle_cpp_streams_poem.txt";
  archive.extract_to(*entry, out_path);
  if (std::filesystem::file_size(out_path) != entry->size()) {
    std::cerr << "extract_to size mismatch\n";
    return 1;
  }
  std::filesystem::remove(out_path);

  auto under_assets = archive.entries_with_prefix("assets/");
  std::cout << "entries under 'assets/': " << under_assets.size() << "\n";

  std::cout << "all checks passed\n";
  return 0;
}
