/* EventToken.h -- the one type WebView2.h needs from the Windows SDK's
 * EventToken.h, which mingw-w64 does not ship. */
#ifndef __eventtoken_h__
#define __eventtoken_h__
typedef struct EventRegistrationToken
{
   __int64 value;
} EventRegistrationToken;
#endif
