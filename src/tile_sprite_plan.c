/*
Tile/Sprite Conversion Plan
===========================

This project currently draws everything in bitmap Mode 4.  The goal is to
switch to using the GBA's tile and sprite engines.  Below is a step by step
outline for the conversion so each stage can be implemented and tested
incrementally.

1. **Baseline Verification**
   - Build and run the current code to ensure the reference game works.
   - Keep a working commit before starting changes for easy comparison.

2. **Select a Tile/Sprite Video Mode**
   - Mode 0 gives up to four tile backgrounds plus sprites.
   - Update `draw_init()` to set `REG_DISPCNT` to Mode 0 with BG0 enabled.
   - Initially fill BG0 with a single tile so we can confirm the mode switch.

3. **Prepare Background Tile Data**
   - Convert the font arrays in `font.h` to 4bpp tile graphics.
   - Reserve character block 0 for these tiles and copy them to VRAM at start.
   - Create a small tile map for the scoreboard digits using BG0.
   - Verify digits appear correctly before moving on.

4. **Set Up Sprite Infrastructure**
   - Define structures for OAM entries and a shadow OAM buffer.
   - Write `sprite_init()` to clear OAM and load paddle/ball tiles into
     character block 4 (object tile memory).
   - Implement `sprite_flush()` to copy the shadow buffer to OAM during VBlank.
   - Test with a single test sprite placed on screen.

5. **Convert Paddles to Sprites**
   - Design a 4bpp tile (or small tile set) for a paddle.
   - For each paddle entity update its OAM attributes instead of drawing a
     rectangle each frame.
   - Confirm movement works with sprites.

6. **Convert the Ball to a Sprite**
   - Create tile graphics for the ball.
   - Replace the ball drawing routine with sprite updates similar to paddles.

7. **Handle Particles as Sprites**
   - Reuse a simple 8x8 tile for all particles.
   - Spawn and update them via OAM just like other entities.

8. **Replace Text Rendering**
   - Adapt `draw_char` and `draw_str` to write tile indices into the BG0 tile
     map rather than setting pixels directly.
   - Remove bitmap oriented helpers such as `set_pixel` and `fill_rect` when no
     longer used.

9. **Final Cleanup and Testing**
   - Ensure `vsync_swap()` only waits for VBlank and handles OAM copying.
   - Play a full game verifying scoring, win conditions and particle effects.
   - Once behavior matches the original version, remove the old framebuffer
     code paths entirely.

Local Testing Instructions
-------------------------
- Run `ninja` to build `hello.gba` after applying each step above.
- Test the ROM in an emulator (for example `mgba hello.gba`) to verify behavior.

Follow Up
---------
- After completing the refactor, reintroduce paddle color cycling by updating
  palette entries each frame.
*/
