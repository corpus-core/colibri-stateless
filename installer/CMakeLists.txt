# CMake configuration for Colibri Server installers

cmake_minimum_required(VERSION 3.10)

# Verify that HTTP_SERVER is enabled (required for installer)
if(NOT HTTP_SERVER)
    message(WARNING "INSTALLER flag is set but HTTP_SERVER is not enabled. Skipping installer targets.")
    return()
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "colibri-server")
set(CPACK_PACKAGE_VENDOR "Corpus Core")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Colibri Stateless Prover Server")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "simon@corpus.io")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/corpus-core/colibri-stateless")

# Installation paths
if(UNIX AND NOT APPLE)
    # Linux
    set(CPACK_INSTALL_PREFIX "/usr")
    set(CONFIG_INSTALL_DIR "/etc/colibri")
    set(SERVICE_INSTALL_DIR "/lib/systemd/system")
    set(DATA_DIR "/var/lib/colibri")
elseif(APPLE)
    # macOS
    set(CPACK_INSTALL_PREFIX "/usr/local")
    set(CONFIG_INSTALL_DIR "/usr/local/etc/colibri")
    set(SERVICE_INSTALL_DIR "/usr/local/share/colibri")
    set(DATA_DIR "/var/lib/colibri")
elseif(WIN32)
    # Windows
    set(CPACK_INSTALL_PREFIX "C:/Program Files/Colibri")
    set(CONFIG_INSTALL_DIR "$ENV{PROGRAMDATA}/Colibri")
endif()

# Install server binary
install(PROGRAMS ${CMAKE_BINARY_DIR}/default/bin/server
        DESTINATION bin
        RENAME colibri-server
        COMPONENT server)

# Install default configuration
install(FILES ${CMAKE_SOURCE_DIR}/installer/config/server.conf.default
        DESTINATION ${CONFIG_INSTALL_DIR}
        RENAME server.conf
        COMPONENT config)

# Platform-specific service files
if(UNIX AND NOT APPLE)
    # Linux: systemd service
    install(FILES ${CMAKE_SOURCE_DIR}/installer/scripts/systemd/colibri-server.service
            DESTINATION ${SERVICE_INSTALL_DIR}
            COMPONENT service)
    
    # Create data directory
    install(DIRECTORY DESTINATION ${DATA_DIR}
            DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                  GROUP_READ GROUP_EXECUTE
                                  WORLD_READ WORLD_EXECUTE
            COMPONENT service)
    
elseif(APPLE)
    # macOS: LaunchDaemon plist
    install(FILES ${CMAKE_SOURCE_DIR}/installer/scripts/launchd/io.corpuscore.colibri-server.plist
            DESTINATION ${SERVICE_INSTALL_DIR}
            COMPONENT service)
endif()

# Debian-specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Corpus Core <simon@corpus.io>")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.27), libssl3 | libssl1.1")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "memcached")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA 
    "${CMAKE_SOURCE_DIR}/installer/linux/debian/postinst"
    "${CMAKE_SOURCE_DIR}/installer/linux/debian/prerm"
    "${CMAKE_SOURCE_DIR}/installer/linux/debian/postrm")

# RPM-specific settings
set(CPACK_RPM_PACKAGE_LICENSE "MIT and PolyForm-Noncommercial-1.0.0")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
set(CPACK_RPM_PACKAGE_REQUIRES "openssl")
set(CPACK_RPM_PACKAGE_SUGGESTS "memcached")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/installer/linux/debian/postinst")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/installer/linux/debian/prerm")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/installer/linux/debian/postrm")

# Determine package type based on platform
if(UNIX AND NOT APPLE)
    # Check if we're on a Debian-based or RPM-based system
    if(EXISTS "/etc/debian_version")
        set(CPACK_GENERATOR "DEB")
    elseif(EXISTS "/etc/redhat-release")
        set(CPACK_GENERATOR "RPM")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PRODUCTBUILD_IDENTITY_NAME "")
    set(CPACK_PRODUCTBUILD_KEYCHAIN_PATH "")
elseif(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_UPGRADE_GUID "12345678-1234-1234-1234-123456789ABC")
    set(CPACK_WIX_PRODUCT_GUID "*")
endif()

# Component descriptions
set(CPACK_COMPONENT_SERVER_DISPLAY_NAME "Server Binary")
set(CPACK_COMPONENT_SERVER_DESCRIPTION "The Colibri server executable")
set(CPACK_COMPONENT_SERVER_REQUIRED ON)

set(CPACK_COMPONENT_CONFIG_DISPLAY_NAME "Configuration")
set(CPACK_COMPONENT_CONFIG_DESCRIPTION "Default server configuration file")
set(CPACK_COMPONENT_CONFIG_REQUIRED ON)

set(CPACK_COMPONENT_SERVICE_DISPLAY_NAME "System Service")
set(CPACK_COMPONENT_SERVICE_DESCRIPTION "Service integration for automatic startup")
set(CPACK_COMPONENT_SERVICE_REQUIRED ON)

# Include CPack
include(CPack)


# Custom target for building installers
add_custom_target(installer
    COMMAND ${CMAKE_CPACK_COMMAND}
    DEPENDS server
    COMMENT "Building installer package"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

message(STATUS "Installer configuration:")
message(STATUS "  Package type: ${CPACK_GENERATOR}")
message(STATUS "  Version: ${CPACK_PACKAGE_VERSION}")
message(STATUS "  Install prefix: ${CPACK_INSTALL_PREFIX}")
message(STATUS "  Config directory: ${CONFIG_INSTALL_DIR}")

