AC_DEFUN([RTEMS_CHECK_NETWORKING],
[dnl
AC_REQUIRE([RTEMS_CANONICAL_TARGET_CPU])dnl
AC_REQUIRE([RTEMS_ENABLE_NETWORKING])dnl

AC_CACHE_CHECK([whether BSP supports networking],
  rtems_cv_HAS_NETWORKING,
  [dnl
    case "$RTEMS_CPU" in
    # do not have address space to hold BSD TCP/IP stack
    avr*|m32c*)
      rtems_cv_HAS_NETWORKING="no"
      ;;
    *)
      if test "${RTEMS_HAS_NETWORKING}" = "yes"; then
        rtems_cv_HAS_NETWORKING="yes";
      else
        rtems_cv_HAS_NETWORKING="disabled";
      fi
      ;;
    esac])
if test "$rtems_cv_HAS_NETWORKING" = "yes"; then
  HAS_NETWORKING="yes";
else
  HAS_NETWORKING="no";
fi
AC_SUBST(HAS_NETWORKING)dnl
])
