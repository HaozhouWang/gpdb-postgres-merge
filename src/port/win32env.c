/*-------------------------------------------------------------------------
 *
 * win32env.c
 *	  putenv() and unsetenv() for win32, which update both process environment
 *	  and caches in (potentially multiple) C run-time library (CRT) versions.
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/port/win32env.c
 *
 *-------------------------------------------------------------------------
 */

#include "c.h"

int
pgwin32_putenv(const char *envval)
{
	char	   *envcpy;
	char	   *cp;
<<<<<<< HEAD

	/*
	 * Each CRT has its own _putenv() symbol and copy of the environment.
	 * Update the environment in each CRT module currently loaded, so every
	 * third-party library sees this change regardless of the CRT it links
	 * against.
	 */
#ifdef _MSC_VER
	typedef int (_cdecl * PUTENVPROC) (const char *);
	static struct
	{
		char	   *modulename;
		HMODULE		hmodule;
		PUTENVPROC	putenvFunc;
	}			rtmodules[] =
	{
		{
			"msvcrt", NULL, NULL
		},						/* Visual Studio 6.0 / MinGW */
		{
			"msvcrtd", NULL, NULL
		},
		{
			"msvcr70", NULL, NULL
		},						/* Visual Studio 2002 */
		{
			"msvcr70d", NULL, NULL
		},
		{
			"msvcr71", NULL, NULL
		},						/* Visual Studio 2003 */
		{
			"msvcr71d", NULL, NULL
		},
		{
			"msvcr80", NULL, NULL
		},						/* Visual Studio 2005 */
		{
			"msvcr80d", NULL, NULL
		},
		{
			"msvcr90", NULL, NULL
		},						/* Visual Studio 2008 */
		{
			"msvcr90d", NULL, NULL
		},
		{
			"msvcr100", NULL, NULL
		},						/* Visual Studio 2010 */
		{
			"msvcr100d", NULL, NULL
		},
		{
			"msvcr110", NULL, NULL
		},						/* Visual Studio 2012 */
		{
			"msvcr120", 0, NULL
		},						/* Visual Studio 2013 */
		{
			"ucrtbase", 0, NULL
		},						/* Visual Studio 2015 and later */
		{
			NULL, 0, NULL
		}
	};
	int			i;

	for (i = 0; rtmodules[i].modulename; i++)
	{
		if (rtmodules[i].putenvFunc == NULL)
		{
			if (rtmodules[i].hmodule == NULL)
			{
				/* Not attempted before, so try to find this DLL */
				rtmodules[i].hmodule = GetModuleHandle(rtmodules[i].modulename);
				if (rtmodules[i].hmodule == NULL)
				{
					/*
					 * Set to INVALID_HANDLE_VALUE so we know we have tried
					 * this one before, and won't try again.
					 */
					rtmodules[i].hmodule = INVALID_HANDLE_VALUE;
					continue;
				}
				else
				{
					rtmodules[i].putenvFunc = (PUTENVPROC) GetProcAddress(rtmodules[i].hmodule, "_putenv");
					if (rtmodules[i].putenvFunc == NULL)
					{
						rtmodules[i].hmodule = INVALID_HANDLE_VALUE;
						continue;
					}
				}
			}
			else
			{
				/*
				 * Module loaded, but we did not find the function last time.
				 * We're not going to find it this time either...
				 */
				continue;
			}
		}
		/* At this point, putenvFunc is set or we have exited the loop */
		rtmodules[i].putenvFunc(envval);
	}
#endif   /* _MSC_VER */

	/*
	 * Update process environment, making this change visible to child
	 * processes and to CRTs initializing in the future.
=======
	typedef int (_cdecl * PUTENVPROC) (const char *);
	static const char *const modulenames[] = {
		"msvcrt",				/* Visual Studio 6.0 / MinGW */
		"msvcrtd",
		"msvcr70",				/* Visual Studio 2002 */
		"msvcr70d",
		"msvcr71",				/* Visual Studio 2003 */
		"msvcr71d",
		"msvcr80",				/* Visual Studio 2005 */
		"msvcr80d",
		"msvcr90",				/* Visual Studio 2008 */
		"msvcr90d",
		"msvcr100",				/* Visual Studio 2010 */
		"msvcr100d",
		"msvcr110",				/* Visual Studio 2012 */
		"msvcr110d",
		"msvcr120",				/* Visual Studio 2013 */
		"msvcr120d",
		"ucrtbase",				/* Visual Studio 2015 and later */
		"ucrtbased",
		NULL
	};
	int			i;

	/*
	 * Update process environment, making this change visible to child
	 * processes and to CRTs initializing in the future.  Do this before the
	 * _putenv() loop, for the benefit of any CRT that initializes during this
	 * pgwin32_putenv() execution, after the loop checks that CRT.
>>>>>>> 9e1c9f959422192bbe1b842a2a1ffaf76b080196
	 *
	 * Need a copy of the string so we can modify it.
	 */
	envcpy = strdup(envval);
	if (!envcpy)
		return -1;
	cp = strchr(envcpy, '=');
	if (cp == NULL)
	{
		free(envcpy);
		return -1;
	}
	*cp = '\0';
	cp++;
	if (strlen(cp))
	{
		/*
		 * Only call SetEnvironmentVariable() when we are adding a variable,
		 * not when removing it. Calling it on both crashes on at least
		 * certain versions of MinGW.
		 */
		if (!SetEnvironmentVariable(envcpy, cp))
		{
			free(envcpy);
			return -1;
		}
	}
	free(envcpy);

	/*
	 * Each CRT has its own _putenv() symbol and copy of the environment.
	 * Update the environment in each CRT module currently loaded, so every
	 * third-party library sees this change regardless of the CRT it links
	 * against.  Addresses within these modules may become invalid the moment
	 * we call FreeLibrary(), so don't cache them.
	 */
	for (i = 0; modulenames[i]; i++)
	{
		HMODULE		hmodule = NULL;
		BOOL		res = GetModuleHandleEx(0, modulenames[i], &hmodule);

		if (res != 0 && hmodule != NULL)
		{
			PUTENVPROC	putenvFunc;

			putenvFunc = (PUTENVPROC) GetProcAddress(hmodule, "_putenv");
			if (putenvFunc)
				putenvFunc(envval);
			FreeLibrary(hmodule);
		}
	}

	/*
	 * Finally, update our "own" cache.  This is redundant with the loop
	 * above, except when PostgreSQL itself links to a CRT not listed above.
	 * Ideally, the loop does visit all possible CRTs, making this redundant.
	 */
	return _putenv(envval);
}

void
pgwin32_unsetenv(const char *name)
{
	char	   *envbuf;

	envbuf = (char *) malloc(strlen(name) + 2);
	if (!envbuf)
		return;

	sprintf(envbuf, "%s=", name);
	pgwin32_putenv(envbuf);
	free(envbuf);
}
