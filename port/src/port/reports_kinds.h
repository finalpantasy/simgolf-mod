#ifndef PORT_REPORTS_KINDS_H
#define PORT_REPORTS_KINDS_H

enum {
  RP_NONE = 0,
  RP_COURSE = 2,     // F1
  RP_COMMENTS = 3,   // F2
  RP_HISTO = 4,      // F3
  RP_KEYS = 5,       // F8
  RP_FINANCE = 6,    // F4
  RP_SGA = 9,        // F7
  RP_MONTH = 8,      // end of month ranking (drawn every frame of the main loop, FUN_0045f870)
  RP_ROSTER = 10,    // F9
  RP_BEST = 20,      // Information > Best scores (FUN_00455a30)
  RP_YEAR = 21,      // end of year course report (FUN_0044fb30(2))
  RP_HOLE = 22,      // hole stats
  RP_TOURNEY = 23,   // tournament results (FUN_0045a090)
  RP_LEADERBOARD = 24, // live tournament standings, nonmodal
  RP_PROFILE = 30,   // golfer profile page (the editor, FUN_004385d0)
  RP_PHOTO = 31,     // snapshot popup, the bulletin board of great shots (FUN_0046e810)
  RP_TRACTS = 32,    // year-end land purchase map (FUN_004587a0)
  RP_SKILLS = 33,    // the pro's skill points screen (FUN_0045f0f0)
  RP_END_YEAR = 34,  // the year summary board (FUN_0044cff0)
  RP_PAIRING = 35,   // choose two golfers for a clubhouse round (FUN_00459850)
  RP_TOP_DESIGNERS = 36, // Top Ten Designers (FUN_00473470)
  RP_ROUTING = 37,      // routing, scenery and home values (FUN_00456be0)
  RP_CREDITS = 38,      // original attribution, native scrolling page (FUN_0044b9c0)
  RP_SHOT_ANALYSIS = 39, // original shot trajectories and native comparison legend
  RP_SNAPSHOT = 40      // automatic SimFoto capture, distinct from the accomplishments board
};

#define RP_REPORT_TAB_TARGETS(X) \
  X(RP_COURSE) \
  X(RP_COMMENTS) \
  X(RP_HISTO) \
  X(RP_SGA) \
  X(RP_FINANCE) \
  X(RP_ROSTER) \
  X(RP_BEST)

#endif
