#pragma once

// The first adventure's page of mysteries (scene 0xc, from the trunk; ARM9
// 0202e358 / run 0202e948 / destructor 0202e598).
//
// How it plays
// ------------
// Ten portraits, one per mystery of the story, two rows of five; a mystery
// shows once the story has raised it (flags 300..309) and changes its
// picture once solved (flags 320..329). A "new" mark sits on mysteries not
// looked at yet. Touching one shows its title and its text on the upper
// screen - the solution's text once it is solved.
namespace picarat::adventure1 {
class Story;

// Runs the page until its close button; the scene returns to the trunk.
void mysteriesPage(Story& story);

} // namespace picarat::adventure1
