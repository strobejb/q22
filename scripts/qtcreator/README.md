# Qt Creator Q22 Struct Highlighting

This directory contains a Qt Creator generic-highlighter definition for q22
Structure Definition Language files (`*.struct`).

## Install

Copy `q22-struct.xml` to Qt Creator's generic highlighter syntax directory:

```powershell
Copy-Item .\scripts\qtcreator\q22-struct.xml `
    C:\Qt\Tools\QtCreator\share\qtcreator\generic-highlighter\syntax\q22-struct.xml
```

Restart Qt Creator after copying the file.

## MIME Setup

Qt Creator does not let users add a new MIME type from the MIME Types settings
page. To avoid C++ diagnostics for `.struct` files:

1. Open `Tools > Options > Environment > MIME Types`.
2. Add `*.struct` to `text/plain`.
3. Do not add `*.struct` to `text/x-chdr` or any other C/C++ MIME type.
4. If Qt Creator asks which editor to use, choose `Plain Text Editor`.

The plain text editor gives the file a non-C++ editor/code-model path, while
`q22-struct.xml` supplies the syntax colouring through the generic highlighter.

The first line of q22 structure definition files is:

```c
// q22-struct v1
```

You can add that as a magic header for `text/plain` if extension matching is not
enough, but the `*.struct` pattern is usually the important part.
