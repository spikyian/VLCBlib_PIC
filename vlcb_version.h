#ifndef _VLCB_VERSION_H_
#define _VLCB_VERSION_H_
/**
 * @copyright Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */
/*
  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
*/

/**
 * @file
 * @brief
 * The version of VLCBlib_PIC.
 * @details
 * Do not edit the numbers by hand. The Release workflow on GitHub
 * (.github/workflows/release.yml) sets them, commits, tags the commit
 * vMAJOR.MINOR.PATCH and creates the GitHub release, so the numbers here always
 * match the tag. Between releases they are those of the last release.
 *
 * These are macros only: they generate no code and use no memory unless a
 * module uses one in its code.
 *
 * A module that needs a particular release can say so in module.h:
 * @code
 * #define VLCBLIB_REQUIRED    VLCBLIB_VERSION_NUMBER(1,0,0)
 * @endcode
 * and building it against an older library stops with an error (see vlcb.h).
 */

#define VLCBLIB_VERSION_MAJOR       0   ///< incompatible changes for modules
#define VLCBLIB_VERSION_MINOR       0   ///< new features, compatible
#define VLCBLIB_VERSION_PATCH       0   ///< fixes only
#define VLCBLIB_VERSION_PRERELEASE  0   ///< 0 for a release, N for a pre-release such as -rcN
#define VLCBLIB_VERSION_STRING      "0.0.0" ///< as the tag, without the v

/** A version as one number, for comparing in #if. */
#define VLCBLIB_VERSION_NUMBER(major, minor, patch) \
    (((major) * 65536L) + ((minor) * 256L) + (patch))

/** This library's version as one number. */
#define VLCBLIB_VERSION \
    VLCBLIB_VERSION_NUMBER(VLCBLIB_VERSION_MAJOR, VLCBLIB_VERSION_MINOR, VLCBLIB_VERSION_PATCH)

#endif /* _VLCB_VERSION_H_ */
