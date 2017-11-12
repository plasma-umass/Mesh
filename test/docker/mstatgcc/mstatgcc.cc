// Copyright Daniel A. Jimienez

/*
 * evilgcc.cc
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>
#include <unistd.h>

// these are the pathnames to the real compilers

#define REAL_GCC	"/usr/bin/clang "
#define REAL_GXX	"/usr/bin/clang++ "
#define REAL_GFORTRAN	"/usr/bin/false "

// this is where we put the real executables

#define EVIL_LAIR	"/spec/native-binaries"
#define DATA_LAIR	"/spec/report/mstat"

// this is the statement you want runspec to use to invoke the real executable

#define PREPEND		"mstat -freq 59 "

int main (int argc, char **argv) {

	// we will build a command into cmd that invokes the real compiler with the args we were passed

	std::string cmd;

	mkdir(EVIL_LAIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	mkdir(DATA_LAIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

	// who are we? gcc, g++, or gfortran?

	if (!strcmp (&argv[0][strlen(argv[0])-3], "gcc"))
		cmd = REAL_GCC;
	else if (!strcmp (&argv[0][strlen(argv[0])-3], "g++"))
		cmd = REAL_GXX;
	else if (!strcmp (&argv[0][strlen(argv[0])-8], "gfortran"))
		cmd = REAL_GFORTRAN;
	else {
		fprintf (stderr, "I give up. I don't know who I am.\n");
		return 1;
	}

	// build the command

	for (int i=1; i<argc; i++) {
		cmd += argv[i];
		cmd += " ";
	}

	// run the command

	int err = system (cmd.c_str());
	if (err)
		return err;

	// go through looking to see if this command would have made an executable.

	for (int i=1; i<argc; i++) {

		// see if we are making an executable (i.e. using -o and not making an object file)

		if (!strcmp (argv[i], "-o") && strcmp (&argv[i+1][strlen(argv[i+1])-2], ".o")) {
			char *binary = argv[i+1];

			// ah-hah! we apparently made an executable. let's kidnap it to our evil lair and leave in its place a doppelganger

			char *binary_path = NULL;
			int err = asprintf(&binary_path, "%s/%s-XXXXXX", EVIL_LAIR, binary);
			if (err < 0) {
				fprintf(stderr, "asprintf failed: %s\n", strerror(errno));
				return 1;
			}
			int fd = mkstemp(binary_path);
			if (fd < 0) {
				fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
				return 2;
			}
			close(fd);
			chmod(binary_path, 0755);

			char s[1000];
			sprintf (s, "cp %s %s\n", binary, binary_path);
			printf ("%s\n", s);
			system (s);

			// make a shell script that does perf or whatever and invokes the real executable

			FILE *f = fopen (binary, "w");
			fprintf (f, "#!/bin/bash\nDATE=`date +%%s.%%N`\nSUFFIX=`cat /dev/urandom | tr -cd 'a-f0-9' | head -c 8`\nexec %s -o %s/%s-$DATE-$SUFFIX.tsv -- %s \"$@\"\n", PREPEND, DATA_LAIR, binary, binary_path);
			fclose (f);

			// make sure it's executable

			chmod (argv[i+1], 0755);
		}
	}
	return 0;
}
