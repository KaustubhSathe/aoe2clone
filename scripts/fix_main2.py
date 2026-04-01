import os

main_cpp_path = os.path.join(os.path.dirname(__file__), '..', 'main.cpp')
with open(main_cpp_path, "r", encoding="utf-8") as f:
    text = f.read()

text = text.replace("const GLint engine.gpu.", "engine.gpu.")
text = text.replace("glGenVertexArrays(1, engine", "glGenVertexArrays(1, &engine")
text = text.replace("glGenBuffers(1, engine", "glGenBuffers(1, &engine")
text = text.replace("glDeleteVertexArrays(1, engine", "glDeleteVertexArrays(1, &engine")
text = text.replace("glDeleteBuffers(1, engine", "glDeleteBuffers(1, &engine")
text = text.replace("const engine.pineTreeScale =", "engine.pineTreeScale =")

with open(main_cpp_path, "w", encoding="utf-8") as f:
    f.write(text)

print("fixed main2")
