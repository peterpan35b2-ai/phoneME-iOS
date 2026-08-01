/* iOS implementation of MIDlet.platformRequest(). */
#include <stddef.h>
#include <string.h>

#include <midlet.h>

extern int phoneme_ios_platform_request(const char* url);

int platformRequest(char* pszUrl) {
    if (pszUrl == NULL) {
        return 0;
    }

    /* MIDP defines an empty string as a request to cancel a pending request.
     * iOS openURL requests cannot be cancelled after submission, so treat the
     * cancellation as successfully handled without launching anything. */
    if (pszUrl[0] == '\0') {
        return 1;
    }

    return phoneme_ios_platform_request(pszUrl) != 0;
}
