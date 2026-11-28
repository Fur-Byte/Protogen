idf_component_register(SRCS "main.cpp" "WearerIO.cpp" "WirelessCrypto.cpp" "WirelessSession.cpp"
  "WirelessPairing.cpp" "SessionHandshake.cpp" "IdentityRecovery.cpp" "WirelessLink.cpp"
  "EspNowTransport.cpp" "SecureStorage.cpp" "PairingIndicator.cpp"
  INCLUDE_DIRS "." "../../PROT_OS7_MAX7219/src/HelmetProtocol"
  REQUIRES esp_wifi esp_event nvs_flash mbedtls esp_timer
    esp_driver_uart esp_driver_gpio esp_driver_i2c esp_driver_spi esp_adc bootloader_support)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)

if(DEFINED PROTOGEN_NATIVE_ROLE)
  if(PROTOGEN_NATIVE_ROLE STREQUAL "helmet")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HELMET_RECEIVER=1)
    if(DEFINED PROTOGEN_NATIVE_UART_DIR)
      target_compile_definitions(${COMPONENT_LIB} PRIVATE PROTOGEN_UART_AUTH=1)
      target_include_directories(${COMPONENT_LIB} PRIVATE "${PROTOGEN_NATIVE_UART_DIR}")
    endif()
  elseif(PROTOGEN_NATIVE_ROLE STREQUAL "paw")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PAW_CONTROLLER=1)
  else()
    message(FATAL_ERROR "Invalid native Protogen role")
  endif()
endif()
