/* Compile with src/enhanced/vr_canopy.c and -If117port/src for a game-free mesh export. */
#include "enhanced/vr_canopy.h"
#include <stdio.h>
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"Usage: export_vr_canopy output.json\n");return 2;}
    if(vr_canopy_selftest())return 1;
    return vr_canopy_export(argv[1])?0:1;
}
