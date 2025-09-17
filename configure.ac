AC_INIT([gobbledegook], [1.0.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AC_PROG_CXX
AC_PROG_RANLIB
AM_PROG_AR

# Require C++20 support
AX_CXX_COMPILE_STDCXX(20, noext, mandatory)

# Check for pthread
AC_CHECK_LIB([pthread], [pthread_create], [], [AC_MSG_ERROR([pthread library not found])])

# Platform detection
AC_CANONICAL_HOST
case "${host_os}" in
    linux*)
        AC_DEFINE([PLATFORM_LINUX], [1], [Define if building on Linux])
        PLATFORM_LINUX=yes
        ;;
    darwin*)
        AC_DEFINE([PLATFORM_MACOS], [1], [Define if building on macOS])
        PLATFORM_MACOS=yes
        AC_MSG_WARN([macOS build - BLE functionality will be limited])
        ;;
    *)
        AC_MSG_ERROR([Unsupported platform: ${host_os}])
        ;;
esac

AM_CONDITIONAL([PLATFORM_LINUX], [test "x$PLATFORM_LINUX" = "xyes"])
AM_CONDITIONAL([PLATFORM_MACOS], [test "x$PLATFORM_MACOS" = "xyes"])

# Platform-specific dependencies
if test "x$PLATFORM_LINUX" = "xyes"; then
    # Use PKG_CHECK_MODULES for proper pkg-config integration on Linux
    PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.50])
    PKG_CHECK_MODULES([GIO], [gio-2.0 >= 2.50])
    PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.50])

    # Check for BlueZ headers (optional)
    AC_CHECK_HEADERS([bluetooth/bluetooth.h], [HAVE_BLUEZ=yes], [HAVE_BLUEZ=no])
    if test "x$HAVE_BLUEZ" = "xno"; then
        AC_MSG_WARN([BlueZ headers not found - install libbluetooth-dev for full functionality])
    fi
else
    # Non-Linux platforms - skip GLib requirements
    AC_DEFINE([NO_GLIB], [1], [Define if GLib is not available])
    AC_MSG_NOTICE([Skipping GLib dependency checks for non-Linux platform])
fi

# Check for std::format availability (C++20 feature)
AC_LANG_PUSH([C++])
AC_CACHE_CHECK([for std::format], [ac_cv_cxx_std_format],
  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <format>
    #include <string>
  ]], [[
    std::string result = std::format("test {}", 42);
  ]])],
  [ac_cv_cxx_std_format=yes],
  [ac_cv_cxx_std_format=no])])

if test "x$ac_cv_cxx_std_format" = "xyes"; then
  AC_DEFINE([HAS_STD_FORMAT], [1], [Define if std::format is available])
else
  AC_DEFINE([HAS_STD_FORMAT], [0], [Define if std::format is not available])
fi
AC_LANG_POP([C++])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_OUTPUT
