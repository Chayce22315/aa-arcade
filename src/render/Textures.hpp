#pragma once

#include <memory>
#include <string>

#include <SDL3/SDL.h>

namespace spindle::render {

struct TextureDeleter {
    void operator()(SDL_Texture* t) const { SDL_DestroyTexture(t); }
};
using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// The few images spindle has (embedded PNGs, see tools/gen_art.py). Everything else is drawn by code.
// A texture that fails to load stays null and the screens draw a procedural stand-in.
class Textures {
public:
    // (Re)creates every texture on this renderer. Returns false and fills *error if any failed.
    bool load(SDL_Renderer* renderer, std::string* error);
    void release();

    SDL_Texture* flame() const { return flame_.get(); }

private:
    TexturePtr flame_;
};

// Embedded path of the flame image.
inline constexpr const char* kFlameImage = "art/flame.png";

} // namespace spindle::render
