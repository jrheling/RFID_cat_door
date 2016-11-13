
modes:

1) regular
 - normally closed, opens briefly when authorized fob is nearby
 
2) locked open
 - always open

3) locked closed
 - always closed
 
4) enroll
 - adds next fob seen to authorized list
 - NB: re-adding an already authorized fob is a no-op
 - NB: returns to regular mode after one fob is added
 
5) training mode
 - enrolls all fobs seen
 - NB: implies open mode
 - NB: stays in this mode until manually changed
 
10) clear
 - removes all fobs from authorized fob list
 - NB: returns to regular mode after clearing list
 
 TO SWITCH MODES:
 - press and hold button for N seconds to move to mode N
 - LED will quickly flash N times (confirmation prompt)
 - press button once within 10s to confirm
 
 MODE INDICATOR:
 - regular mode: yellow solid
 - open: green solid
 - closed: red solid
 - enroll: green/red flash alternately
 - training: yellow flash
