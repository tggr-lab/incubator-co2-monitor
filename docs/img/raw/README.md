Framebuffer captures taken from the board with `tools/screenshot.py`, 320x240.

To refresh them with the board connected:

    tools/screenshot.py --settle 200 --little --all docs/img/raw/screen

then rename `screen_main.png` and `screen_graph.png` to `main.png` and
`graph.png`, and run `tools/make_build_guide.py`.
