# PDF Parser C

This is a simple project aimed to showcase how to extract information from a
PDF file.

The project uses nix to manage dependencies:
- zlib <https://github.com/madler/zlib> - needed to uncompress plain text data

If you have nix installed you can just `nix develop` to create a shell with
everything ready to go.

### Quickstart

```console
gcc main.c -o main -lz
```

Plans include: adding args to specify the filepath, dumping the results in a
nicer format + some refactoring (taking into account the dictionary values of
each stream: text/image etc), maybe looking into how to insert exe files into
pdfs.
