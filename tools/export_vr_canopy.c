/* Compile with src/enhanced/vr_canopy.c and -If117port/src for a game-free mesh export. */
#include "enhanced/vr_canopy.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
    int pilot=argc==3&&!strcmp(argv[1],"--pilot");
    if(argc!=2&&!pilot){fprintf(stderr,"Usage: export_vr_canopy [--pilot] output.json\n");return 2;}
    if(vr_canopy_selftest())return 1;
    return (pilot?vr_canopy_pilot_export(argv[2]):vr_canopy_export(argv[1]))?0:1;
}
