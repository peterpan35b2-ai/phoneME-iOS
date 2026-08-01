/*
 *
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

#include <limits.h>
#include <string.h>

#include <suspend_resume_lcdui.h>
#include <lcdlf_export.h>
#include <midpEventUtil.h>
#include <midp_properties_port.h>
#include <midpMalloc.h>

const char _locale_str[] = "microedition.locale";


/**
 * Default implementation of suspending routine for the LCDUI.
 */
MIDPError suspend_lcdui(void *resource) {
    const char* locale;
    size_t len;

    LCDUIState *st = (LCDUIState*) resource;
    /* Saving lcdui state */
    st->isDisplayRotated =
        lcdlf_get_reverse_orientation(lcdlf_get_current_hardwareId());

    locale = getSystemProperty(_locale_str);
    if (locale == NULL) {
        locale = "";
    }
    len = strlen(locale);
    if (len >= UINT_MAX) {
        return OUT_OF_MEMORY;
    }

    if (st->locale != NULL) {
        midpFree(st->locale);
        st->locale = NULL;
    }
    st->locale = midpMalloc((unsigned int)(len + 1U));
    if (st->locale == NULL) {
        return OUT_OF_MEMORY;
    }

    memcpy(st->locale, locale, len + 1U);

    return ALL_OK;
}

/**
 * Default implementation of resuming routine for the LCDUI.
 */
MIDPError resume_lcdui(void *resource) {
    const char* locale;
    size_t len;
    MidpEvent midpEvent;    
    LCDUIState *st = (LCDUIState*) resource;
    /* Restoring lcdui state */
    jboolean orient = lcdlf_get_reverse_orientation(lcdlf_get_current_hardwareId());
    
    if (orient != st->isDisplayRotated) {
        MIDP_EVENT_INITIALIZE(midpEvent);
        midpEvent.type    = ROTATION_EVENT;
        midpStoreEventAndSignalForeground(midpEvent);
    }
    
    locale = getSystemProperty(_locale_str);
    if (locale == NULL) {
        locale = "";
    }
    len = strlen(locale);
      
    if (st->locale == NULL ||
            len != strlen(st->locale) ||
            memcmp(locale, st->locale, len) != 0) {
       MIDP_EVENT_INITIALIZE(midpEvent);
       midpEvent.type    = CHANGE_LOCALE_EVENT;
       StoreMIDPEventInVmThread(midpEvent, -1);
     }
	  
     if (st->locale != NULL) {                                                                                                                                                
        midpFree(st->locale);
	    st->locale = NULL;
     } 
    
    return ALL_OK;
}
