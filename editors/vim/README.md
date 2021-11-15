# Rune Vim Syntax

This directory contains the necessary syntax files for vim. Vim support is still
new to Rune and there are likely still bugs abound. Please file any complaints
to /dev/nu^Waidenhall@ and/or g/rune-lang. There are 2 methods for
setting these files up for your environment.

## create symlinks within your ~/.vim directory

This will ensure you get the most up to date versions of both files every time
you open a Rune file.
  - `mkdir ~/.vim/syntax && ln -s /google/src/head/depot/google3/third_party/rune/editors/vim/syntax/rune.vim ~/.vim/syntax/rune.vim`
  - `mkdir ~/.vim/ftdetect && ln -s /google/src/head/depot/google3/third_party/rune/editors/vim/ftdetect/rune.vim ~/.vim/ftdetect/rune.vim`

# Copy files to your .vim directory

This method won't provide you with updates to these syntax files.
  - `mkdir ~/.vim/syntax && cp /google/src/head/depot/google3/third_party/rune/editors/vim/syntax/rune.vim ~/.vim/syntax/rune.vim`
  - `mkdir ~/.vim/ftdetect && cp /google/src/head/depot/google3/third_party/rune/editors/vim/ftdetect/rune.vim ~/.vim/ftdetect/rune.vim`
