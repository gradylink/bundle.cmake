#include "bundle_assets.h"

#include <stdio.h>
#include <stdlib.h>

static void dump(const bundle_archive *archive, const char *path) {
  bundle_entry entry;
  int rc = bundle_find(archive, path, &entry);
  if (rc != BUNDLE_OK) {
    fprintf(stderr, "bundle_find(%s) failed: %d\n", path, rc);
    exit(1);
  }

  size_t size;
  char *data = (char *)bundle_load(&entry, &size);
  if (!data) {
    fprintf(stderr, "bundle_load(%s) failed\n", path);
    exit(1);
  }

  printf("--- %s (%zu bytes, method=%u) ---\n%.*s\n", path, size, entry.method, (int)size, data);
  free(data);
}

int main(void) {
  bundle_archive archive = bundle_assets_archive();

  printf("entries in bundle: %ld\n", bundle_entry_count(&archive));

  bundle_iterator it;
  bundle_iter_begin(&archive, &it);
  bundle_entry entry;
  while (bundle_iter_next(&it, &entry) == 1) {
    char name[256];
    bundle_entry_name(&entry, name, sizeof(name));
    printf("  - %s (%zu bytes)\n", name, entry.uncompressed_size);
  }

  dump(&archive, "assets/hello.txt");
  dump(&archive, "assets/nested/world.txt");
  return 0;
}
