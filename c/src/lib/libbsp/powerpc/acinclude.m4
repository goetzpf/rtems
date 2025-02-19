# RTEMS_CHECK_BSPDIR(RTEMS_BSP_FAMILY)
AC_DEFUN([RTEMS_CHECK_BSPDIR],
[
  case "$1" in
  beatnik )
    AC_CONFIG_SUBDIRS([beatnik]);;
  gen5200 )
    AC_CONFIG_SUBDIRS([gen5200]);;
  gen83xx )
    AC_CONFIG_SUBDIRS([gen83xx]);;
  haleakala )
    AC_CONFIG_SUBDIRS([haleakala]);;
  mbx8xx )
    AC_CONFIG_SUBDIRS([mbx8xx]);;
  motorola_powerpc )
    AC_CONFIG_SUBDIRS([motorola_powerpc]);;
  mpc55xxevb )
    AC_CONFIG_SUBDIRS([mpc55xxevb]);;
  mpc8260ads )
    AC_CONFIG_SUBDIRS([mpc8260ads]);;
  mvme3100 )
    AC_CONFIG_SUBDIRS([mvme3100]);;
  mvme5500 )
    AC_CONFIG_SUBDIRS([mvme5500]);;
  psim )
    AC_CONFIG_SUBDIRS([psim]);;
  qemuppc )
    AC_CONFIG_SUBDIRS([qemuppc]);;
  qoriq )
    AC_CONFIG_SUBDIRS([qoriq]);;
  ss555 )
    AC_CONFIG_SUBDIRS([ss555]);;
  t32mppc )
    AC_CONFIG_SUBDIRS([t32mppc]);;
  tqm8xx )
    AC_CONFIG_SUBDIRS([tqm8xx]);;
  virtex )
    AC_CONFIG_SUBDIRS([virtex]);;
  virtex4 )
    AC_CONFIG_SUBDIRS([virtex4]);;
  virtex5 )
    AC_CONFIG_SUBDIRS([virtex5]);;
  *)
    AC_MSG_ERROR([Invalid BSP]);;
  esac
])
