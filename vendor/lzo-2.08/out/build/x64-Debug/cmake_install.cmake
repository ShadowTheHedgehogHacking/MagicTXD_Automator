# Install script for directory: C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/out/install/x64-Debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/doc" TYPE FILE FILES
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/AUTHORS"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/COPYING"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/NEWS"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/THANKS"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/doc/LZO.FAQ"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/doc/LZO.TXT"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/doc/LZOAPI.TXT"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/lzo" TYPE FILE FILES
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1a.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1b.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1c.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1f.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1x.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1y.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo1z.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo2a.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzo_asm.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzoconf.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzodefs.h"
    "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/include/lzo/lzoutil.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/out/build/x64-Debug/lzo2.lib")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "C:/Users/dreamsyntax/Desktop/MagicTXD_ShadowAutomator/vendor/lzo-2.08/out/build/x64-Debug/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
