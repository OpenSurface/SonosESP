/**
 * UI Radio Mode Handler
 * Functions to adapt UI for radio stations vs music tracks
 */

#pragma once

// Check if currently playing radio
bool isCurrentlyRadio();

// Set UI to radio or music mode
void setRadioMode(bool enable);

// Update UI based on current track type (call when track changes)
void updateRadioModeUI();
