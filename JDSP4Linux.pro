TEMPLATE = subdirs

SUBDIRS += \
    libjamesdsp \
    liveprog_tests \
    src

liveprog_tests.subdir = libjamesdsp/tests
liveprog_tests.depends = libjamesdsp
src.depends = libjamesdsp
