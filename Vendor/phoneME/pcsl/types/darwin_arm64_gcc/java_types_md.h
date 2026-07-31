/* Native Java primitive types for macOS arm64 PCSL. */

#if !defined _JAVA_TYPES_H_
# error "Never include <java_types_md.h> directly; use <java_types.h> instead."
#endif

#ifndef _JAVA_TYPES_MD_H_
#define _JAVA_TYPES_MD_H_

#ifndef _JAVASOFT_JNI_H_
typedef signed char    jbyte;
typedef unsigned short jchar;
typedef int            jint;
typedef long long      jlong;
#endif

#define PCSL_LLD "%lld"

#endif /* !_JAVA_TYPES_MD_H_ */
