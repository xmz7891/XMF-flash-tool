#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* Hand-written replacement for the CMake-generated miniz_export.h template.
 * miniz is compiled as a static library inside this project, so no symbol
 * visibility attributes are needed. */

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT
#define MINIZ_DEPRECATED

#endif /* MINIZ_EXPORT_H */
