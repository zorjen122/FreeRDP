# FreeRDP: RDP2VIR Custom Business DVC Virtual Channel

define_channel("rdp2vir")
set(CHANNEL_NAME "mytest" PARENT_SCOPE)

if(WITH_CLIENT_CHANNELS)
  add_channel_client(${MODULE_PREFIX} ${CHANNEL_NAME})
endif()

# if(WITH_SERVER_CHANNELS)
#   add_channel_server(${MODULE_PREFIX} ${CHANNEL_NAME})
# endif()