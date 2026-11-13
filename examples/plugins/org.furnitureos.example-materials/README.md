# Example plugin: generic boards and edge bands

A declarative data plugin (ADR-0007 phase 1) contributing one `.foslib` library with generic
materials and edge bands. All entity ids are namespaced `org.furnitureos.example-materials:<local-id>`.

The library file is generated, not hand-written, so that its checksum is correct:

    cmake --build <build> --target fos_plugins_make_example
    <build>/src/plugins/fos_plugins_make_example examples/plugins/org.furnitureos.example-materials

The test `ExamplePlugin.CommittedLibraryIsReproducible` fails if the committed file differs from a fresh export.
See `docs/api/plugins.md`.
