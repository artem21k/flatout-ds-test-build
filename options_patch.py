#!/usr/bin/env python3
"""Options menu rework.  Usage: python3 options_patch.py <main.c> <audio.h> <out dir>
   - Options pages (like the console version): GAME (AI level, rally laps), SOUND (SFX volume, music volume, channels
     stereo / mono / headphones), CONTROLS (button map + two presets).  The computer / video icon is gone, no vibration.
   - SFX and music sliders work (music at 0 % stops the stream completely, engine + interface sounds follow the SFX slider)
   - control presets are used by the game: 1 = A gas, B brake, R nitro, L handbrake / 2 = R gas, L brake, A nitro, B handbrake
"""
import sys, os

main_c, audio_h, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
s = open(main_c).read()
a = open(audio_h).read()


def rep(txt, old, new, name):
    assert txt.count(old) == 1, (name, txt.count(old))
    return txt.replace(old, new)


# ------------------------------------------------------------------------------------------------ globals
s = rep(s, "static int   lapsN=3, sndOn=1;\n",
        "static int   lapsN=3, sndOn=1;\n"
        "static int   sfxVol=10, musVol=5, chMode=0;                  /* sliders 0..10, channels: 0 stereo, 1 mono, 2 headphones */\n"
        "static int   ctlPreset=0, optTab=1, sliderDrag=-1;           /* control preset 0/1, options page 0 game / 1 sound / 2 controls */\n"
        "static int   gTouchHeld=0, gTx=0, gTy=0;                     /* touch screen state every frame (for dragging sliders) */\n", "globals")

# ------------------------------------------------------------------------------------------------ icon: Nintendo DS
s = rep(s, "    } else if(kind==9){                            /* (old crash burst, unused) */",
        "    } else if(kind==7){                            /* Nintendo DS: two screens, d-pad and buttons */\n"
        "        u16 sc=C15(255,214,70);\n"
        "        hrect(cx-11,cy-13,22,12,c); hrect(cx-8,cy-11,16,8,sc);\n"
        "        hrect(cx-11,cy,22,13,c);    hrect(cx-5,cy+2,10,8,sc);\n"
        "        hrect(cx-10,cy+6,4,1,sc); hrect(cx-9,cy+4,1,5,sc);\n"
        "        hrect(cx+6,cy+4,2,2,sc); hrect(cx+8,cy+6,2,2,sc); hrect(cx+6,cy+8,2,2,sc);\n"
        "    } else if(kind==9){                            /* (old crash burst, unused) */", "icon")

# ------------------------------------------------------------------------------------------------ options screen
a0 = s.index("static void hudOpt(void)")
a1 = s.index("/* ============================== IN-GAME HUD")
hud = r'''static const char *optTabName[3]={"GAME OPTIONS","SOUND OPTIONS","CONTROLS"};
static const int   optTabIcon[3]={0,4,7};                         /* flag, loudspeaker, Nintendo DS */
static const char *chName[3]={"STEREO","MONO","HEADPHONES"};
static const char *ctlTxt[2][4]={{"HANDBRAKE","NITRO","GAS","BRAKE/REV"},{"BRAKE/REV","GAS","NITRO","HANDBRAKE"}};   /* L, R, A, B per preset */

static void hOptRow(int y,int sel,const char *label)
{
    hrect(8,y,240,22,sel?C15(220,128,18):C15(34,27,15));
    hframe(8,y,240,22,sel?COL_WHITE:C15(120,84,14));
    htextEx(16,y+7,label,sel?COL_DARK:COL_CREAM,1,1);
}
static void hOptValue(int y,int sel,const char *v){ htextEx(242-htextW(v,1),y+7,v,sel?COL_DARK:COL_YEL,1,1); }
static void hOptSlider(int y,int sel,const char *label,int v)       /* v = 0..10 */
{
    char b[8]; int f=v*78/10;
    hOptRow(y,sel,label);
    hrect(122,y+6,80,10,sel?C15(150,84,10):C15(12,9,4));
    hrect(123,y+7,f,8,sel?COL_WHITE:COL_YEL);
    hframe(122,y+6,80,10,sel?COL_DARK:COL_EDGE);
    siprintf(b,"%d%%",v*10);
    hOptValue(y,sel,b);
}
static void hudOptControls(void)                                   /* button map of the chosen preset */
{
    const char **t=ctlTxt[ctlPreset];
    hrect(4,30,248,86,C15(24,19,10)); hframe(4,30,248,86,C15(120,84,14)); hrect(125,34,1,78,C15(120,84,14));
    hHint(9,36,'L',t[0]); hHint(9,52,'R',t[1]); hHint(9,68,'A',t[2]); hHint(9,84,'B',t[3]);
    htextS(9,100,"TOUCH",COL_YEL,1); htextS(57,100,"MENU",COL_WHITE,1);
    hHint(132,36,'Y',"LOOK BACK"); hHint(132,52,'X',"RESET CAR");
    htextS(132,68,"D-PAD",COL_YEL,1);  htextS(180,68,"STEER",COL_WHITE,1);
    htextS(132,84,"SELECT",COL_YEL,1); htextS(188,84,"CAMERA",COL_WHITE,1);
    htextS(132,100,"START",COL_YEL,1); htextS(180,100,"PAUSE",COL_WHITE,1);
}

static void hudOpt(void)
{
    int i,w; char b[32];
    hcopy(menuBg,0,HUDH);
    hHeader("OPTIONS");
    if(optTab==0){
        hOptRow(34,optSel==0,"AI LEVEL");    siprintf(b,"<  %s  >",diffName[diff]); hOptValue(34,optSel==0,b);
        hOptRow(60,optSel==1,"RALLY LAPS");  siprintf(b,"<  %d  >",lapsN);          hOptValue(60,optSel==1,b);
    } else if(optTab==1){
        hOptSlider(34,optSel==0,"SFX VOLUME",sfxVol);
        hOptSlider(60,optSel==1,"MUSIC VOLUME",musVol);
        hOptRow(86,optSel==2,"CHANNELS");    siprintf(b,"<  %s  >",chName[chMode]); hOptValue(86,optSel==2,b);
    } else {
        hudOptControls();
    }
    for(i=0;i<3;i++) hButton(61+i*50,122,i==optTab,optTabIcon[i]);
    hHint(24,135,'L',""); hHint(216,135,'R',"");
    hrect(0,158,256,11,C15(20,15,8));                               /* caption strip */
    hline(0,158,255,158,C15(120,80,10)); hline(0,168,255,168,C15(120,80,10));
    if(optTab==2) siprintf(b,"PRESET %d",ctlPreset+1); else siprintf(b,"%s",optTabName[optTab]);
    w=htextW(b,1);
    htextC(128,160,b,COL_WHITE,1);
    htri(128-w/2-9,160,-1,COL_YEL); htri(128+w/2+4,160,1,COL_YEL);
    hFooter("NEXT PAGE","BACK",'B');
}

'''
s = s[:a0] + hud + s[a1:]

# ------------------------------------------------------------------------------------------------ option changes
b0 = s.index("static void optChange(int dir)")
b1 = s.index("static void menuOk(void)")
chg = r'''static int optClamp(int v){ return v<0?0:(v>10?10:v); }
static void optChange(int dir)
{
    if(optTab==0){
        if(optSel==0) diff=(diff+dir+3)%3;
        else lapsN=(dir>0)?(lapsN<3?3:(lapsN<5?5:2)):(lapsN>3?3:(lapsN>2?2:5));
    } else if(optTab==1){
        if(optSel==0) sfxVol=optClamp(sfxVol+dir);
        else if(optSel==1){ musVol=optClamp(musVol+dir); musApply(); }
        else chMode=(chMode+dir+3)%3;
    } else ctlPreset^=1;
    sndUi(0);
    hudTimer=0;
}
static void optSlider(int which,int tx)                            /* tap / drag on a slider bar */
{
    int v=optClamp(((tx-123)*10+39)/78), *p=(which==0)?&sfxVol:&musVol;
    if(*p==v) return;
    *p=v;
    if(which==1) musApply();
    sndUi(0);
    hudTimer=0;
}
'''
s = s[:b0] + chg + s[b1:]

s = rep(s, "        startGame(lastMap);\n    }\n    hudTimer=0;\n}\nstatic void menuBack",
        "        startGame(lastMap);\n    } else if(menuScr==SC_OPT){\n        optTab=(optTab+1)%3; optSel=0;                        /* A = next options page */\n    }\n    hudTimer=0;\n}\nstatic void menuBack", "menuOk")

s = rep(s, "    } else {\n        for(i=0;i<3;i++) if(ty>=40+i*30&&ty<64+i*30){ optSel=i; optChange(tx<128?-1:1); }\n    }\n}",
        r'''    } else {                                                         /* options */
        if(ty>=118&&ty<158){
            for(i=0;i<3;i++) if(tx>=58+i*50&&tx<98+i*50){ optTab=i; optSel=0; sndUi(0); hudTimer=0; }
        } else if(ty>=158){
            if(optTab==2){ ctlPreset^=1; sndUi(0); hudTimer=0; }
        } else if(optTab==0){
            for(i=0;i<2;i++) if(ty>=34+i*26&&ty<56+i*26){ optSel=i; optChange(tx<128?-1:1); }
        } else if(optTab==1){
            for(i=0;i<3;i++) if(ty>=34+i*26&&ty<56+i*26){
                optSel=i;
                if(i<2 && tx>=112 && tx<=208){ sliderDrag=i; optSlider(i,tx); }
                else optChange(tx<128?-1:1);
            }
        } else if(ty>=30&&ty<116){ ctlPreset^=1; sndUi(0); hudTimer=0; }
    }
}''', "menuTouch")

s = rep(s, "    } else {\n        if(up) optSel=(optSel+2)%3;\n        if(dn) optSel=(optSel+1)%3;\n        if(left)  optChange(-1);\n        if(right) optChange(1);\n    }",
        r'''    } else {                                                         /* options: L / R = page, up / down = row, left / right = value */
        int rows=(optTab==0)?2:((optTab==1)?3:0);
        if(down&KEY_L){ optTab=(optTab+2)%3; optSel=0; sndUi(0); hudTimer=0; }
        if(down&KEY_R){ optTab=(optTab+1)%3; optSel=0; sndUi(0); hudTimer=0; }
        if(rows){
            if(up) optSel=(optSel+rows-1)%rows;
            if(dn) optSel=(optSel+1)%rows;
        }
        if(left)  optChange(-1);
        if(right) optChange(1);
        if(sliderDrag>=0){ if(gTouchHeld) optSlider(sliderDrag,gTx); else sliderDrag=-1; }
    }''', "menuInput")

# ------------------------------------------------------------------------------------------------ gameplay buttons (presets)
s = rep(s, "                thr=(held&(KEY_A|KEY_UP))?1.0f:0.0f;\n                if(held&(KEY_B|KEY_DOWN)) thr=-1.0f;",
        "                thr=(held&((ctlPreset?KEY_R:KEY_A)|KEY_UP))?1.0f:0.0f;\n                if(held&((ctlPreset?KEY_L:KEY_B)|KEY_DOWN)) thr=-1.0f;", "gas")
s = rep(s, "                nit=(held&KEY_R)!=0;\n                hb=(held&KEY_L)!=0;",
        "                nit=(held&(ctlPreset?KEY_A:KEY_R))!=0;\n                hb=(held&(ctlPreset?KEY_B:KEY_L))!=0;", "nitro")

# ------------------------------------------------------------------------------------------------ touch screen state every frame
s = rep(s, "        if(down&KEY_TOUCH) touchRead(&tp);\n        gameFrame(held,down,(down&KEY_TOUCH)!=0,tp.px,tp.py);",
        "        if(held&KEY_TOUCH) touchRead(&tp);\n        gTouchHeld=(held&KEY_TOUCH)!=0; gTx=tp.px; gTy=tp.py;\n        gameFrame(held,down,(down&KEY_TOUCH)!=0,tp.px,tp.py);", "touch")

# ------------------------------------------------------------------------------------------------ audio.h
a = rep(a, "    if(!sndOn) return -1;\n    if(vol<0) vol=0;", "    if(!sndOn||sfxVol<=0) return -1;\n    if(vol<0) vol=0;", "sfx1")
a = rep(a, "    if(pan>127) pan=127;\n",
        "    if(pan>127) pan=127;\n"
        "    vol=vol*sfxVol/10;                                          /* SFX slider */\n"
        "    if(chMode==1) pan=64;                                       /* mono: everything in the centre */\n"
        "    else if(chMode==2) pan=64+(pan-64)*6/10;                    /* headphones: softer left / right separation */\n", "sfx2")
a = rep(a, "(float)(ENG_VOL_MAX-ENG_VOL_IDLE)*clampf(engRpm,0.0f,1.0f)));", "(float)(ENG_VOL_MAX-ENG_VOL_IDLE)*clampf(engRpm,0.0f,1.0f))*sfxVol/10);", "eng")
a = rep(a, "soundSetVolume(rollCh,(int)clampf(spd*4.5f,0.0f,100.0f));", "soundSetVolume(rollCh,(int)clampf(spd*4.5f,0.0f,100.0f)*sfxVol/10);", "roll")
a = rep(a, "soundSetVolume(skidCh,(int)clampf(slip*9.0f,0.0f,85.0f));", "soundSetVolume(skidCh,(int)clampf(slip*9.0f,0.0f,85.0f)*sfxVol/10);", "skid")
a = rep(a, "#define MUS_VOL    64                    /* music volume 0..127 (sfx are louder)             */",
        "#define MUS_VOL    64                    /* (old fixed music volume)                          */\n"
        "#define MUSV       (127*musVol/10)       /* music volume 0..127 from the MUSIC slider (5 = old default) */", "musv")
a = rep(a, "MUS_RATE,MUS_VOL,64,true,0);", "MUS_RATE,MUSV,64,true,0);", "musstart")
a = rep(a, "static void musPlayMenu(void){ if(!sndOn||!musFsOk) return;", "static void musPlayMenu(void){ if(!sndOn||!musFsOk||musVol<=0) return;", "pm")
a = rep(a, "    if(!sndOn||!musFsOk) return;\n    musIsMenu=0;", "    if(!sndOn||!musFsOk||musVol<=0) return;\n    musIsMenu=0;", "pr")
a = rep(a, "    if(!sndOn){ musStop(); return; }\n    c=TIMER_DATA(3);", "    if(!sndOn||musVol<=0){ musStop(); return; }\n    c=TIMER_DATA(3);", "mu")
a = rep(a, "static void musUpdate(void)\n{", "/* the MUSIC slider moved: 0 stops the stream, anything else sets the volume (or starts the menu theme again) */\n"
        "static void musApply(void)\n{\n    if(musVol<=0){ musStop(); return; }\n    if(musCh>=0) soundSetVolume(musCh,MUSV);\n    else if(state==ST_MENU) musPlayMenu();\n}\n\nstatic void musUpdate(void)\n{", "apply")
a = rep(a, "static void sndInit(void){}\n", "static void sndInit(void){}\nstatic void musApply(void){}\n", "stub")

open(os.path.join(out, "main.c"), "w").write(s)
open(os.path.join(out, "audio.h"), "w").write(a)
print("ok")
