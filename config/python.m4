#
# Autoconf macros for configuring the build of Python extension modules
#
<<<<<<< HEAD
# config/python.m4
=======
# $PostgreSQL: pgsql/config/python.m4,v 1.15 2009/01/04 00:54:15 petere Exp $
>>>>>>> b0a6ad70a12b6949fdebffa8ca1650162bf0254a
#

# PGAC_PATH_PYTHON
# ----------------
# Look for Python and set the output variable 'PYTHON'
# to 'python' if found, empty otherwise.
AC_DEFUN([PGAC_PATH_PYTHON],
[AC_PATH_PROG(PYTHON, python)
if test x"$PYTHON" = x""; then
  AC_MSG_ERROR([Python not found])
fi
])


# _PGAC_CHECK_PYTHON_DIRS
# -----------------------
# Determine the name of various directories of a given Python installation.
AC_DEFUN([_PGAC_CHECK_PYTHON_DIRS],
[AC_REQUIRE([PGAC_PATH_PYTHON])
AC_MSG_CHECKING([for Python distutils module])
if "${PYTHON}" -c 'import distutils' 2>&AS_MESSAGE_LOG_FD
then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([distutils module not found])
fi
AC_MSG_CHECKING([Python configuration directory])
<<<<<<< HEAD
python_majorversion=`${PYTHON} -c "import sys; print(sys.version[[0]])"`
=======
>>>>>>> b0a6ad70a12b6949fdebffa8ca1650162bf0254a
python_version=`${PYTHON} -c "import sys; print(sys.version[[:3]])"`
python_configdir=`${PYTHON} -c "from distutils.sysconfig import get_python_lib as f; import os; print(os.path.join(f(plat_specific=1,standard_lib=1),'config'))"`
python_includespec=`${PYTHON} -c "import distutils.sysconfig; print('-I'+distutils.sysconfig.get_python_inc())"`

AC_SUBST(python_majorversion)[]dnl
AC_SUBST(python_version)[]dnl
AC_SUBST(python_configdir)[]dnl
AC_SUBST(python_includespec)[]dnl
# This should be enough of a message.
AC_MSG_RESULT([$python_configdir])
])# _PGAC_CHECK_PYTHON_DIRS


# PGAC_CHECK_PYTHON_EMBED_SETUP
# -----------------------------
#
# Note: selecting libpython from python_configdir works in all Python
# releases, but it generally finds a non-shared library, which means
# that we are binding the python interpreter right into libplpython.so.
# In Python 2.3 and up there should be a shared library available in
# the main library location.
AC_DEFUN([PGAC_CHECK_PYTHON_EMBED_SETUP],
[AC_REQUIRE([_PGAC_CHECK_PYTHON_DIRS])
AC_MSG_CHECKING([how to link an embedded Python application])

python_libdir=`${PYTHON} -c "import distutils.sysconfig,string; print(' '.join(filter(None,distutils.sysconfig.get_config_vars('LIBDIR'))))"`
python_ldlibrary=`${PYTHON} -c "import distutils.sysconfig,string; print(' '.join(filter(None,distutils.sysconfig.get_config_vars('LDLIBRARY'))))"`
python_so=`${PYTHON} -c "import distutils.sysconfig,string; print(' '.join(filter(None,distutils.sysconfig.get_config_vars('SO'))))"`
ldlibrary=`echo "${python_ldlibrary}" | sed "s/${python_so}$//"`

if test x"${python_libdir}" != x"" -a x"${python_ldlibrary}" != x"" -a x"${python_ldlibrary}" != x"${ldlibrary}"
then
	# New way: use the official shared library
	ldlibrary=`echo "${ldlibrary}" | sed "s/^lib//"`
	# special for greenplum... python was built in /opt/, but resides in the ext directory
	if test ! -d "${python_libdir}"
	then
		python_libdir=`echo "${python_configdir}" | sed "s/\/python2.7\/config//"`
	fi
	python_libspec="-L${python_libdir} -l${ldlibrary}"
else
	# Old way: use libpython from python_configdir
	python_libdir="${python_configdir}"
	python_libspec="-L${python_libdir} -lpython${python_version}"
fi

python_additional_libs=`${PYTHON} -c "import distutils.sysconfig,string; print(' '.join(filter(None,distutils.sysconfig.get_config_vars('LIBS','LIBC','LIBM','LOCALMODLIBS','BASEMODLIBS'))))"`

AC_MSG_RESULT([${python_libspec} ${python_additional_libs}])

AC_SUBST(python_libdir)[]dnl
AC_SUBST(python_libspec)[]dnl
AC_SUBST(python_additional_libs)[]dnl

# threaded python is not supported on OpenBSD
AC_MSG_CHECKING(whether Python is compiled with thread support)
pythreads=`${PYTHON} -c "import sys; print(int('thread' in sys.builtin_module_names))"`
if test "$pythreads" = "1"; then
  AC_MSG_RESULT(yes)
  case $host_os in
  openbsd*)
    AC_MSG_ERROR([threaded Python not supported on this platform])
    ;;
  esac
else
  AC_MSG_RESULT(no)
fi

])# PGAC_CHECK_PYTHON_EMBED_SETUP
