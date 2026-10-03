// Minimal stand-in for <curl/curl.h>.
// chiaki-ng only needs curl for "Remote Play over Internet" (PSN hole punching and
// certificate fetching). This UWP build is LAN-only, so those sources are not compiled;
// the shared headers only need the CURL type name to exist.
#ifndef PSXBOX_CURL_SHIM_H
#define PSXBOX_CURL_SHIM_H

typedef void CURL;

#endif
