CONFIG += ordered
TEMPLATE = subdirs
SUBDIRS += 7zip installer

# order of compiling
installer.depends = 7zip
