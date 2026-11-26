OBSPETS {version}
===============

A small pet for your stream. It walks along the bottom of your screen and reacts to follows,
subs, bits, raids, channel points and your chat. Its reactions to chat use credit on your
Stream Tools account (https://sindruid.com/streamtools/obspets); everything else needs none.

Needs: OBS Studio 33 or newer on Windows 10 or 11.

The easy way to install it is the installer from https://sindruid.com/streamtools/obspets
This zip is for installing it by hand.


Install by hand
---------------
1. Close OBS.
2. Copy the obspet folder from this zip into
       C:\ProgramData\obs-studio\plugins
   (Make the plugins folder if it isn't there. Windows may ask for permission: click Continue.)
   You should end up with C:\ProgramData\obs-studio\plugins\obspet\obspet.dll.
3. Open OBS. In the Tools menu, click "OBSPETS: Add a pet to this scene".
   (Or under Sources, click +, then OBSPETS, then OK.)
4. In the window that opens, click "Sign in with Twitch" (or Kick) and follow the page in your
   browser.

To update, close OBS and replace the obspet folder with the new one.
To uninstall, close OBS and delete C:\ProgramData\obs-studio\plugins\obspet. Your settings,
characters and sign-in are kept in %APPDATA%\obs-studio\plugin_config\obspet; delete that folder
too if you want everything gone.


Things to try
-------------
- Channel points: once signed in, the window has a button that makes a "Pet <name>" reward
  on your channel. (Channel points need Twitch Affiliate or Partner.)
- "Try a reaction" shows what it does for a follow, a raid, a chat message and so on.
- You and your stream both hear it. "Who hears it" changes that (pick "Only your stream" if
  you hear it twice). Its volume is in OBS's Audio Mixer, under OBSPETS.
- If you move or resize its box, it offers to fit it back to the screen: it's made to fill the
  whole screen, and "Size" sets how big it is.
- "Pets" adds up to 6 pets. Pick who reacts (taking turns, at random, or all of them), and how
  many are on screen at once: the rest swap in, jumping on as another jumps off.
- Each pet's "Reactions" list turns a reaction on or off for that pet: pick it to flip it.
- More settings are under "Show all settings".
- How it reacts to chat, its personality and its limits are on
      https://sindruid.com/streamtools/obspets


Your own characters
-------------------
Tools, "OBSPETS: Pet editor" makes and changes characters in your browser.
Under "Show all settings", "Open my characters folder" opens
    %APPDATA%\obs-studio\plugin_config\obspet\packs
Put a character's folder there. To make one, copy obspet\data\packs\_template from this zip
and follow the README.md next to it. After changing its files, click "Reload the character".


Licence
-------
See SOURCE.txt, LICENSE.txt and THIRD-PARTY.txt.
