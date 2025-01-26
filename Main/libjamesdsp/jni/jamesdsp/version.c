#include "version_note.h"

/* Library version needs to be added in the name member of note_type structure in below format
 * "lib.ver.1.0.0." + "<library_name>" + ":" + "<version>"
 */
const lib_ver_note_t so_ver __attribute__ ((section (".note.lib.ver")))
      __attribute__ ((visibility ("default"))) = {
  100,
  0,
  0,
  "lib.ver.1.0.0.libjamedsp_skel.so:1.0.0",
};
