TEMPLATE = subdirs

SUBDIRS += \
    libjamesdsp \
    libjamesdsp/tests \
    src

libjamesdsp/tests.depends = libjamesdsp
src.depends = libjamesdsp
