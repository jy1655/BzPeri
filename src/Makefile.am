# Build a static library (libggk.a)
noinst_LIBRARIES = libggk.a

# Platform-specific configuration
if PLATFORM_LINUX
libggk_a_CPPFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GOBJECT_CFLAGS)
else
libggk_a_CPPFLAGS = -DNO_GLIB
endif

libggk_a_CXXFLAGS = -fPIC -Wall -Wextra -O2 -Wpedantic -std=c++20

# Common sources for all platforms
COMMON_SOURCES = DBusInterface.cpp \
                 DBusInterface.h \
                 DBusMethod.cpp \
                 DBusMethod.h \
                 DBusObject.cpp \
                 DBusObject.h \
                 DBusObjectPath.h \
                 FormatCompat.cpp \
                 FormatCompat.h \
                 GattCharacteristic.cpp \
                 GattCharacteristic.h \
                 GattDescriptor.cpp \
                 GattDescriptor.h \
                 GattInterface.cpp \
                 GattInterface.h \
                 GattProperty.cpp \
                 GattProperty.h \
                 GattService.cpp \
                 GattService.h \
                 GattUuid.h \
                 Globals.h \
                 Gobbledegook.cpp \
                 ../include/Gobbledegook.h \
                 Init.cpp \
                 Init.h \
                 Logger.cpp \
                 Logger.h \
                 Server.cpp \
                 Server.h \
                 ServerTypes.cpp \
                 ServerTypes.h \
                 ServerUtils.cpp \
                 ServerUtils.h \
                 Utils.cpp \
                 Utils.h

# Platform-specific sources
if PLATFORM_LINUX
PLATFORM_SOURCES = BluezAdapter.cpp \
                   BluezAdapter.h \
                   BluezTypes.cpp \
                   BluezTypes.h \
                   BluezAdvertisement.cpp \
                   BluezAdvertisement.h
else
PLATFORM_SOURCES =
endif

libggk_a_SOURCES = $(COMMON_SOURCES) $(PLATFORM_SOURCES)
# Platform-specific programs
if PLATFORM_LINUX
noinst_PROGRAMS = standalone

# Build our standalone server (linking statically with libggk.a, linking dynamically with GLib)
standalone_CPPFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GOBJECT_CFLAGS)
standalone_CXXFLAGS = -fPIC -Wall -Wextra -O2 -Wpedantic -std=c++20
standalone_SOURCES = standalone.cpp
standalone_LDADD = libggk.a $(GLIB_LIBS) $(GIO_LIBS) $(GOBJECT_LIBS)

# HCI packet parser tests removed - no longer needed with BlueZ D-Bus implementation
else
# Non-Linux platforms - no BLE programs
noinst_PROGRAMS =
endif
