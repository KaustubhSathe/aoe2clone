import os

main_cpp_path = os.path.join(os.path.dirname(__file__), '..', 'main.cpp')
with open(main_cpp_path, "r", encoding="utf-8") as f:
    text = f.read()

# Fix double replacement
text = text.replace("engine.gpu.engine.gpu.", "engine.gpu.")

# Fix variable declarations which had 'engine.' prefixed but kept their type
text = text.replace("std::optional<TextureFrame> engine.pineTreeFrame", "engine.pineTreeFrame")
text = text.replace("std::optional<TextureFrame> engine.townCenterFrame", "engine.townCenterFrame")

# Fix blockedTileTranslations assignment
text = text.replace("const engine.blockedTileTranslations =", "engine.blockedTileTranslations =")
text = text.replace("std::vector<glm::vec2> engine.blockedTileTranslations =", "engine.blockedTileTranslations =")

with open(main_cpp_path, "w", encoding="utf-8") as f:
    f.write(text)

print("fixed main.cpp")
