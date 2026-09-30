#pragma once

/* Runtime metadata shared by CLI, About, and exported diagnostics.
   application.fam keeps the numeric FAP version required by uFBT. */
#define TAMA_APP_VERSION "3.2.1"
#define TAMA_PROTOCOL_VERSION "1"
#define TAMA_APP_CAPABILITIES \
  "connection_ir,connection_legacy,connection_legacy_v4,connection_sniffer," \
  "friends_lf,friends_progress,standalone_ui,hybrid_cli"
