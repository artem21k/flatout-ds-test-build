Flatout DS
=  In GARAGE - car selection with scales: TOP SPEED, ACCELERATION,
HANDLING, STRENGTH, WEIGHT, NITRO, class (DERBY/RACE) and drive.
In OPTIONS - AI level, number of laps, sound.
CARS (8, 4 classes): DERBY - ROAMER (rusty pickup), MALICE (black coupe #17) |
RACE - NEVADA (blue pickup #23, 4WD), INSETTA (white sedan with wing) |
STREET - BULLET GT (black coupe with flames), CANYON (blue pickup with stripes) |
BONUS - TRUCK (black truck tractor), FLATMOBILE (silver wedge).
 Low-poly models: the body is a "frame of sections" (7-8 sections with their own
height/width), the wheels are octagonal prisms flush with the body, all
irregularities (doors, windows, stripes, rust, grilles, numbers, flames) are drawn
in a 128x64 texture for each car. Faraway cars are drawn in a simplified way.
The characteristics really affect the ride, and each car has its own collision radius.
The models are my interpretations "in the spirit" of the reference, not files from the game.
MAPS (4): DERBY STADIUM / JUNKYARD | RALLY FOREST / CANYON (desert)
The maps are author's, "in the spirit" of Flatout 2 (real maps cannot be transferred to the DS).

 CONTROLS
Menu: D-pad - select, A - forward, B - back, START - quick start,
touching icons/cards/carousel also works
Game: Left/Right steering wheel A or Up gas
B or Down brake/back Y or R nitro (charged from impacts)
L handbrake (drift) X turn on the track (rally)
START or touch "MENU" - exit menu

HOW TO ASSEMBLE .nds
Option A (PC): install devkitPro (devkitARM + libnds), take the ARM9 template from
examples/nds/templates, put source/main.c, source/textures.h and source/cars.h in its
source/, in the devkitPro terminal run make.
 Option B (phone, GitHub): create a repository, upload main.c,
textures.h and cars.h (to the root), and put the github_workflow/build.yml file (your working script + cars.h) as
.github/workflows/build.yml. Next, the Actions tab -> Build NDS -> Run workflow;
the finished .nds will be in Artifacts.

If the linker complains about sinf/cosf/atan2f: in the Makefile LIBS := -lnds9 -lm

IF A COMPILATION ERROR OCCURS
At the beginning of main.c there are two switches in the comment:
#define ENABLE_SOUND 0 - removes all sound
#define USE_RAW_MATRIX_REGS 1 - another way to call translate/scale
Uncomment and compile again. If it doesn't help - send the error text.

 WHAT IS TESTED AND WHAT IS NOT
Tested on PC (with stubs instead of libnds and own rasterizer):
- all 4 maps with different cars are passed from start to finish without NaN and crashes;
- menu: keys, touch, all screens, car selection, options;
- DS limits: max. ~1900 vertices and ~480 polygons per frame (allowed ~6000/2000),
vertex coordinates in the DS range, textures ~155 KB with 256 KB VRAM;
- picture: preview/ folder - frames from my rasterizer (approximation, not emulator).
NOT tested: assembly with real devkitARM/libnds and running in emulator or on
hardware. I estimated the speed by eye (should pull 60 fps), but I did not measure it.

 MEMORY (original DS: 4 MB RAM, 656 KB VRAM)
Textures ~235 KB out of 256 KB (VRAM banks A+B), subscreen 96 KB (bank C).
RAM: ~0.6 MB out of 4 MB (code, texture data, two 96 KB lower screen buffers).
Geometry per frame: ~4200 vertices / ~1060 polygons (limit ~6144 / 2048).

TEXTURES
tools/gen_textures.py + tools/carspec.py (Python + Pillow, numpy, scipy) generate
source/textures.h and source/cars.h (model data). Car shapes - in carspec.py:
all textures are procedural (grass, soil, sand, concrete, road, pine, cactus,
clean and rusty body, sky...) and interface font.

RUMBLE FOREST DS  -  гра для ОРИГІНАЛЬНОЇ Nintendo DS (не DSi) у дусі Flatout 2
==============================================================================

ЩО В ГРІ
  Верхній екран: 3D з текстурами - небо з пагорбами, земля, дорога, дерева-спрайти,
  кактуси, каміння, бетонні стіни, контейнери, шини, бочки, машини з колесами,
  тіні, пил, іскри, дим від побитих машин, полум'я нітро, стартова арка.
  Нижній екран: меню в стилі Flatout (жовто-чорні шеврони, сепія, курсивний шрифт,
  іконки), під час гонки - мінікарта, швидкість, здоров'я, нітро, коло/позиція.
  Звук: двигун (висота залежить від швидкості), удари, відлік.

  МЕНЮ: Головне меню (RALLY / DERBY / GARAGE / OPTIONS) -> вибір траси чи арени ->
        гонка. У GARAGE - вибір машини зі шкалами: TOP SPEED, ACCELERATION,
        HANDLING, STRENGTH, WEIGHT, NITRO, клас (DERBY/RACE) і привід.
        У OPTIONS - рівень ШІ, кількість кіл, звук.
  МАШИНИ (8, 4 класи): DERBY - ROAMER (іржавий пікап), MALICE (чорне купе №17) |
        RACE - NEVADA (синій пікап №23, 4WD), INSETTA (білий седан з антикрилом) |
        STREET - BULLET GT (чорне купе з полум'ям), CANYON (синій пікап зі смугами) |
        BONUS - TRUCK (чорний сідловий тягач), FLATMOBILE (сріблястий клин).
        Моделі low-poly: кузов - це "каркас з перерізів" (7-8 перерізів з власною
        висотою/шириною), колеса - восьмикутні призми врівень з кузовом, усі
        нерівності (двері, вікна, смуги, іржа, решітки, номери, полум'я) намальовані
        в текстурі 128x64 на кожну машину. Далекі машини малюються спрощено.
        Характеристики реально впливають на їзду, а в кожної машини свій радіус зіткнень.
        Моделі - мої інтерпретації "в дусі" референсу, не файли з гри.
  МАПИ (4):  DERBY  STADIUM / JUNKYARD   |   RALLY  FOREST / CANYON (пустеля)
  Мапи авторські, "в дусі" Flatout 2 (справжні мапи неможливо перенести на DS).

КЕРУВАННЯ
  Меню:  D-pad - вибір, A - далі, B - назад, START - швидкий старт,
         дотик по іконках/картках/каруселі теж працює
  Гра:   Вліво/Вправо кермо      A або Вгору  газ
         B або Вниз   гальмо/назад   Y або R   нітро (заряджається від ударів)
         L ручник (дрифт)        X  повернути на трасу (ралі)
         START або дотик "MENU" - вихід у меню

ЯК ЗІБРАТИ .nds
  Варіант А (ПК): встанови devkitPro (devkitARM + libnds), візьми шаблон ARM9 з
    examples/nds/templates, поклади source/main.c, source/textures.h і source/cars.h у його
    source/, у терміналі devkitPro виконай  make.
  Варіант Б (телефон, GitHub): створи репозиторій, завантаж туди main.c,
    textures.h і cars.h (в корінь), а файл github_workflow/build.yml (твій робочий скрипт + cars.h) поклади як
    .github/workflows/build.yml. Далі вкладка Actions -> Build NDS -> Run workflow;
    готовий .nds буде в Artifacts.

  Якщо лінкер скаржиться на sinf/cosf/atan2f: у Makefile  LIBS := -lnds9 -lm

ЯКЩО ВИЛІЗЕ ПОМИЛКА КОМПІЛЯЦІЇ
  На початку main.c є два перемикачі в коментарі:
    #define ENABLE_SOUND 0         - прибирає весь звук
    #define USE_RAW_MATRIX_REGS 1  - інший спосіб викликати translate/scale
  Розкоментуй і збери знову. Якщо не допомогло - надішли текст помилки.

ЩО ПЕРЕВІРЕНО І ЩО НІ
  Перевірено на ПК (з заглушками замість libnds і власним растеризатором):
  - всі 4 мапи з різними машинами проходяться від старту до кінця без NaN і вильотів;
  - меню: клавіші, дотик, всі екрани, вибір машини, опції;
  - ліміти DS: макс. ~1900 вершин і ~480 полігонів за кадр (дозволено ~6000/2000),
    координати вершин у діапазоні DS, текстури ~155 КБ з 256 КБ VRAM;
  - картинка: папка preview/ - кадри з мого растеризатора (наближення, не емулятор).
  НЕ перевірено: збирання справжнім devkitARM/libnds та запуск в емуляторі чи на
  залізі. Швидкодію оцінював на око (має тягнути 60 к/с), але не заміряв.

ПАМ'ЯТЬ (оригінальна DS: 4 МБ RAM, 656 КБ VRAM)
  Текстури ~235 КБ з 256 КБ (банки VRAM A+B), підекран 96 КБ (банк C).
  RAM: ~0.6 МБ з 4 МБ (код, дані текстур, два буфери нижнього екрана по 96 КБ).
  Геометрія за кадр: ~4200 вершин / ~1060 полігонів (ліміт ~6144 / 2048).

ТЕКСТУРИ
  tools/gen_textures.py + tools/carspec.py (Python + Pillow, numpy, scipy) генерують
  source/textures.h і source/cars.h (дані моделей). Форми машин - у carspec.py:
  всі текстури процедурні (трава, ґрунт, пісок, бетон, дорога, сосна, кактус,
  чистий і іржавий кузов, небо...) і шрифт інтерфейсу.
