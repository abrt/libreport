# Hacking on Libreport

Here's where to get the code:

    $ git clone https://github.com/abrt/libreport.git
    $ cd libreport/

The remainder of the commands assumes you're in the top level of the
Libreport git repository checkout.

## Building

1. Install dependencies

    First, you should install all dependent packages.

    Dependencies can be listed by:

        $ ./autogen.sh sysdeps

    or installed by:

        $ ./autogen.sh sysdeps --install

    The dependency installer gets the data from [the rpm spec file](libreport.spec.in).

2. Build from source

    When you have all dependencies installed you can now build an rpm package by using these commands:

        $ ./autogen.sh
        $ make rpm

    Now, in the `build` folder, you can find rpm packages both in `noarch` and `x86_64` (or your
            architecture) directories.


### Testing

The easiest way how to test everything (build, run tests) is to build an rpm (see,
        *2. Build from source in Building chapter*).

For running only tests execute:

    ./autogen.sh
    make check

(Note: If you put any arguments to `./autogen.sh` command, also put `--prefix=/usr` for all tests to
 work correctly)

If you need to only rerun one specific test:

    cd tests
    AUGEAS_LENS_LIB="/usr/share/augeas/lenses:<clone_path>/data/augeas" ./testsuite <n>

where \<clone\_path\> is the path to your top level of the Libreport git repository checkout and \<n\> is
the number of the test.

If you make changes in a test, before running it again, execute:

    make  atconfig atlocal ./testsuite

## Contributing a change

### Basic git workflow:

1. Fork the Libreport repository (hit fork button on https://github.com/abrt/libreport)

2. Clone your fork

3. Create and check out to a new branch in your clone (`git checkout -b <name_of_branch>`)

4. ... make changes...

5. Test your changes

6. Create tests for the given changes

7. Add edited files (`git add <file_name>`)

8. Create commit (`git commit`) [How to write a proper git commit
   message](https://chris.beams.io/posts/git-commit/)

9. Push your branch (`git push -u origin <name_of_branch>`)

10. Go to https://github.com/abrt/libreport and click `Compare & pull request`

11. Create the PR

12. Wait for review
