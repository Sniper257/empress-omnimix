EMPRESS MEGAMIX - 공개 베타
==========================

KONAMI 또는 BEMANI와 관계가 없으며, 공식 승인이나 지원을 받지 않은 팬 프로젝트입니다.
모든 상표의 권리는 각 소유자에게 있습니다. MEGAMIX는 무료입니다.

MEGAMIX를 사용하기 전에 원본 세이브를 반드시 백업하세요.
버그로 세이브가 손상될 가능성이 아직 조금은 남아 있습니다.
홈브류 메모리 카드 파일 매니저로 세이브를 꺼내 다른 장치에 백업을 보관하세요.
PCSX2를 사용한다면 가상 메모리 카드 파일을 백업하세요.

알려진 버그와 제한 사항
----------------------

- 메모리 부족으로 일부 곡이 로딩 중 멈출 수 있습니다.
- Floating Hi-Speed와 LIFT를 함께 쓰면 문제가 생길 수 있습니다.
  녹숫(GN) 조절이 잘 안 되면 Lock Cover를 꺼 주세요.
- LIFT는 레인 커버와 같은 이미지를 사용합니다.
- ELF가 MEGAMIX 폴더 밖에 있으면 런처 설정이 제대로 저장되지 않을 수 있습니다.
- STATISTICS와 RIVAL 메뉴는 수정하지 않아 정상적으로 동작하지 않을 가능성이 있습니다.
- MEGAMIX의 전체 해금은 GALLERY에는 적용되지 않습니다.
- BEGINNER 메뉴에는 추가곡이 나오지 않습니다.

버그 제보
---------

GitHub Issues에 아래 내용을 포함해 제보해 주세요.
https://github.com/marysiamzawka/empress-megamix/issues

- 콘솔 모델명(예: SCPH-50004).
- 저장장치(USB 또는 MMCE).
- MEGAMIX 빌드에 포함한 작품.
- 연결해 두었던 컨트롤러.
- 사용 중인 launcher.cfg. 컨트롤러 문제라면 controllers.0과 controllers.1도 첨부해 주세요.
- 메모리 카드의 세이브 폴더 BISLPM-55221BM2DX16.
  원본 EMPRESS 세이브 파일만 보내지 말고 폴더 전체를 보내 주세요.
- 발생한 문제와 재현 방법. 해당하는 경우 곡, 난이도, 플레이 모드도 알려 주세요.

설정 파일은 보통 MEGAMIX 폴더에 있습니다. ELF를 다른 곳에서 실행한다면 런처에서 선택한
설정 저장 위치를 확인하세요. MMCE에서는 MEGAMIX가 사용하는 가상 메모리 카드에서
세이브를 꺼내 주세요.

준비
----

필요한 것:

- Python 3.10 이상.
- 직접 소유한 EMPRESS + PREMIUM BEST Disc 1(EMPRESS)의 ISO 백업.
  다른 지원 PS2 작품도 직접 덤프한 ISO로 곡을 추가할 수 있습니다.
- PS2에서 홈브류를 실행할 수 있는 환경 또는 PCSX2.
  부팅 방법이 필요하다면 FMCB, PS2BBL, FreeDVDBoot 등을 찾아보세요.
- 여유 공간이 5 GB 이상인 USB 드라이브 또는 MMCE 장치(PSxMemCard Gen2 등).
  기본 설치는 약 4.3 GiB, 모든 지원 디스크를 포함하면 약 36.2 GiB입니다.

지원하는 덤프의 Redump SHA-1 해시는 다음과 같습니다.

    3rd style      495f5f9fc6b62562c92b27be31c7d1a9bae64b31
    4th style      69c73667a7b2fc6e6fd2314899377f62cf16d534
    5th style      abda772a65b7d4ed0d7175358af9ca9209c03d37
    6th style      0010d44b65c5eeb9360c9eace11f1ba840c01c58
    7th style      090ca87e447106368c0c89f63e68a4b6c318b5d9
    8th style      01d2a7ffa427d73996b4a19fab14d1c351012377
    9th style      6d42643fb992c19737b22813166a12b5d60bd69e
    10th style     680edcac40ed72ed318bc1bb8d6e89224b80a330
    IIDX RED       8e52f963a33c9fe95022861ef39966bde715ed1a
    HAPPY SKY      b82e7939b720b8a1c0875e88a328493f336a758f
    DistorteD      706da91072b75262729c96b16aa91181231184d2
    GOLD           7cc3625f57f4d77d67ac3fb3675d3ee5de18e726
    DJ TROOPERS    6a35e096900dc6e7842f7aa86366ba0f5fe78dfd
    EMPRESS        92d95574a6a35a908cc3255a997a15f126993ada
    PREMIUM BEST   b23f9799b2cb6106b45c992928c97d6f2e4d3a8b
    beatmania USA  497cd0a20420d270047a77009efc7c48ce95f727

기본 설치 만들기
----------------

빌더 ZIP을 풀고 파일들을 같은 폴더에 둡니다. 그 폴더에서 터미널을 열고 실행하세요.

    python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX"

Windows에서는 python3 대신 py를 사용하세요.

    py megamix.py "D:\ISOs\EMPRESS DVD1.iso" "D:\MEGAMIX"

출력 경로는 새 폴더로 지정하세요. Python만 있으면 됩니다.
기본 폰트로 생성할 때는 SDK, 컴파일러, 추가 패키지가 필요하지 않습니다.
사용자 폰트를 쓰려면 기본 설치를 만들기 전에 아래 설정을 마치세요.

사용자 폰트 (선택)
------------------

기본 설치를 만들기 전에 .ttf, .otf, .ttc, .otc를 해당 폴더에 넣으세요.

    fonts/compacta/   큰 곡명과 곡명 카드 (Compacta BT)
    fonts/eurostile/  선곡 바의 곡명과 카드의 장르
    fonts/handel/     Eurostile보다 우선 적용
    fonts/helvetica/  장르와 아티스트명
    fonts/gothic/     일본어 고딕체 (DF HeiSei Gothic)
    fonts/mincho/     곡명과 카드 아티스트명용 일본어 명조체 (DF HeiSei Mincho)

파일명은 상관없습니다. OS에 폰트를 설치하거나 JSON을 수정할 필요도 없습니다.
없는 글꼴/문자는 동봉된 대체 폰트를 사용합니다. 각 폴더에 다른 폰트를 넣어도 됩니다.
빌더가 메타데이터에서 서체와 굵기를 선택하고 사용 내역을 표시합니다.

기본 설치를 만드는 PC에 추가 패키지를 설치하세요. Windows에서는:

    py -m pip install Pillow fonttools

Linux/macOS의 시스템 관리 Python에서는 가상 환경을 사용하세요.

    python3 -m venv .venv
    .venv/bin/python -m pip install Pillow fonttools

이 환경을 쓸 때는 이후 명령의 python3 대신 .venv/bin/python을 사용하세요.
평소대로 기본 설치를 만들면 fonts 폴더를 자동으로 찾습니다. 다른 위치를 쓰려면
--fonts "path/to/fonts", 사용자 폰트를 무시하려면 --no-custom-fonts를 추가하세요.

추가 팩은 같은 빌더로 평소대로 생성하고 설치하세요. 기본 설치의 변환된 폰트 데이터를
사용하므로 추가 설치 시 원본 폰트나 추가 패키지가 없어도 됩니다. 기본 설치보다 먼저
추가 팩을 만들어도 됩니다. 추가 팩 명령에 --fonts는 필요 없습니다.
지정하면 같은 폰트 프로필을 쓰는 기본 설치가 필요합니다.

기본 설치를 복사할 때 .megamix/fonts도 유지하세요. 원본 폰트 파일은 게임이나 팩에
복사되지 않습니다. 생성되는 텍스트만 바뀌며 원본 이미지, 런처 UI, 시작 화면의 문구는
그대로입니다. 폰트를 바꾸려면 새 기본 설치를 만들고 추가 팩을 다시 설치하세요.
생성된 이미지만 직접 덮어쓰지 마세요.

기존 설치 업데이트하기
----------------------

데이터 계약이 같은 새 릴리스는 이미 만든 기본 설치와 팩을 다시 만들지 않고
런처, 런타임, 모드만 교체할 수 있습니다:

    python3 megamix.py update "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX"

기본 설치를 만들 때 쓴 ISO를 지정하세요(런처 리소스 두 개만 읽습니다). 설치된 팩,
설정, 세이브, 곡 폴더는 그대로 유지됩니다. 새 기본 설치가 필요한 릴리스라면 그렇게
알리고 아무것도 바꾸지 않습니다. 런처 캐시를 쓰는 경우 업데이트 후 다시 만드세요.

직접 추가한 변환 곡
-------------------

변환한 곡 폴더를 songs/<스타일>/ 에 복사한 뒤, 그 스타일의 index.txt에 폴더 이름을,
songs/styles.txt에 스타일 이름을 적고 다음을 실행하세요:

    python3 megamix.py index "path/to/MEGAMIX"

그러면 verify, cache, install, update 후에도 목록에 남습니다.

다른 디스크에서 곡 추가하기
--------------------------

추가 팩을 만든 다음 기본 설치에 적용하세요.

    python3 megamix.py "path/to/HAPPY SKY.iso" "path/to/HAPPY-SKY-pack"
    python3 megamix.py install "path/to/HAPPY-SKY-pack" "path/to/MEGAMIX"

추가할 디스크마다 반복하면 됩니다. 순서는 상관없습니다.
팩의 파일을 직접 복사하지 말고 install 명령을 사용하세요.

완성된 MEGAMIX 폴더를 USB 드라이브 또는 MMCE 카드에 복사한 뒤 안전하게 제거하세요.
ISO와 빌더 파일은 PC에 두면 됩니다.

에뮬레이터 / HostFs 빌드
------------------------

PCSX2용으로는 별도의 HostFs 기본 설치를 만드세요.

    python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX-host" --target host

PCSX2에서 HostFs를 켜고 출력된 MEGAMIX.ELF를 실행하세요.
같은 추가 팩과 install 명령으로 곡을 추가할 수 있습니다.
콘솔용과 HostFs용 설치는 따로 두세요.

파일 검증과 중단 후 재개
-----------------------

설치된 파일을 검증하려면:

    python3 megamix.py verify "path/to/MEGAMIX"

생성이나 설치가 중단되면 같은 명령을 다시 실행해 이어갈 수 있습니다.
작업 중인 폴더와 복구 저널은 지우지 마세요. Ctrl+C로 정상 종료할 수 있습니다.
강제 종료 후 잠금 파일이 남았다면 기존 프로세스가 종료됐는지 확인하고,
오류 메시지에 나온 잠금 파일만 삭제하세요.

PC에서 런처 캐시 생성 (선택)
----------------------------

추가한 디스크 수에 따라 실기 첫 부팅에 오래 걸릴 수 있습니다.

PC에서 캐시를 미리 생성하면 이 시간을 줄일 수 있습니다.

명령을 실행할 Python 환경에 Wasmtime을 설치하세요.

    python3 -m pip install "wasmtime>=50.0.1,<51"

Windows에서는 python3 대신 py를 사용하세요. 시스템 관리 Python에서는 위의 가상 환경을
사용하세요. PC의 폴더와 PS2에서 실제로 쓰는 정확한 게임 데이터 경로를 지정합니다.

    python3 megamix.py cache "X:\MEGAMIX" --game-path mmce0:/MEGAMIX/
    python3 megamix.py cache "/media/USB/Games/BM" --game-path mass:/Games/BM/

--game-path에는 ELF 위치가 아닌 PS2의 게임 데이터 경로를 쓰세요.
MMCE는 슬롯 1이 mmce0:, 슬롯 2가 mmce1:이며 USB는 mass:입니다.
mass:와 mass0:의 차이까지 런처의 경로와 정확히 맞추세요. 다르면 캐시를 다시 만듭니다.
예: mmce1:/MEGAMIX/, mass:/Games/BM/. HOST는 host:입니다.

PS2에서 사용할 launcher.cfg를 기준으로 생성하세요. 설정이 다른 위치에 있다면:

    python3 megamix.py cache "X:\MEGAMIX" --game-path mmce1:/MEGAMIX/ --config-dir "Y:\APPS\MEGAMIX" --settings-path mc0:/APPS/MEGAMIX/

--config-dir은 PC의 설정 폴더, --settings-path는 그 폴더의 PS2 경로입니다.
debug.cfg는 debug 0, prepprobe 0으로 두세요. 미완료 설정 저장이나 팩 설치/업데이트가
있으면 먼저 마치세요. 설치 잠금이 있거나 기본 파일이 손상됐으면 캐시를 생성하지 않습니다.

실행
----

평소 사용하는 홈브류 런처에서 MEGAMIX.ELF를 실행하세요.
ELF는 게임 폴더 밖에 있어도 됩니다. 게임 파일을 찾지 못하면 저장장치 탐색기에서
MEGAMIX 폴더를 선택하세요.

런처 조작
---------

- DualShock: 방향키로 이동/값 변경, ×로 선택.
- beatmania 컨트롤러: 1번 아래, 2번 위, 6번 선택.
- SELECT로 PS2에 연결한 beatmania 컨트롤러용 조작을 켜거나 끕니다.
- START로 게임을 시작합니다. 하위 메뉴에는 Exit 항목도 있습니다.

PS2 컨트롤러와 설정된 USB 컨트롤러 모두 런처 조작에 사용할 수 있습니다.

메인 메뉴 항목:

- Video: 480i, 448라인 480p, 전체 480라인 480p.
  런처도 선택한 모드를 사용합니다. CRT에서 480p를 쓰려면 수평 주파수 31 kHz를 지원하는
  디스플레이와 컴포넌트 케이블, 또는 Sync-on-Green을 지원하는 RGB 연결이 필요합니다.
- Songs: 설치된 곡 폴더를 켜거나 끕니다.
- Skin settings: 레인 밝기, 키 빔 길이, 노트 이미지, 노트 높이, 프레임 팩의 초기 설정.
  더 자세한 설정은 게임 내 스킨 에디터에서 변경할 수 있습니다.
- Autosave: 기존의 종료 시 저장 또는 선곡 화면으로 돌아온 뒤 저장 중에서 선택합니다.
  게임 내 AUTOSAVE도 켜야 합니다.
- Bind USB controller: 각 플레이어의 USB 컨트롤러를 지정하고 설정합니다.
- Change game file location: 다른 설치 위치를 선택합니다. 변경 후 런처를 다시 실행하세요.

첫 실행은 캐시를 생성하므로 더 오래 걸립니다. 다음부터는 캐시를 재사용합니다.
디스크를 추가하거나 사용할 폴더를 변경하면 캐시를 다시 만들 수 있습니다.

USB 컨트롤러
------------

플레이어를 고르고 DualShock, 7-key, 5-key 중 하나를 선택한 뒤 조작을 할당하세요.
턴테이블은 버튼, 스틱형 축, 회전식 절대값 축을 사용할 수 있습니다.
할당 단계 사이에는 버튼 등을 놓아 주세요. 저장 전에 미할당 또는 중복 할당 경고를 확인하세요.

일반적인 PC용 beatmania 컨트롤러를 포함해 표준 USB HID 컨트롤러를 지원합니다.
독자 규격이나 일부 장치는 동작하지 않을 수 있습니다.
같은 모델 두 대를 포함해 컨트롤러 두 대를 각각 다른 플레이어에 지정할 수 있습니다.

게임 내에서는 각 플레이어가 PS2 입력 또는 USB 입력 중 하나를 사용합니다.
게임 중에 USB 컨트롤러를 재설정하거나 교체하려면 런처에서 USB를 지정해 두세요.
게임 실행 후에는 PS2 입력과 USB 입력을 서로 전환할 수 없습니다.

게임 내
-------

기본 흐름은 EMPRESS와 같습니다. 추가 기능은 대부분 FREE 모드에 있습니다.
곡과 기존 CUSTOMIZE 아이템은 처음부터 해금되어 있습니다.

MOD MENU
--------

선곡 화면에서 다음 버튼을 동시에 누르면 열립니다.

- 싱글 / 2인 배틀: 자신의 컨트롤러에서 1 + 3 + 5번.
- DP: 양쪽의 1 + 3번.
- DualShock: □ + × + ○. DP는 두 컨트롤러 모두에서 □ + ×.
  기본 버튼 배치 기준입니다.

버튼을 모두 놓은 뒤 조합을 입력하세요.

메뉴 조작:

- DualShock: 방향키, ○ 선택, □ 뒤로.
- 7-key: 1번 아래, 2번 위, 6번 선택.
- 5-key: 1번 아래, 2번 위, 5번 선택.

값을 선택해 편집하고, 다시 선택하면 편집이 끝납니다.
START 또는 SAVE / EXIT는 저장 후 종료, SELECT 또는 EXIT는 변경 취소 후 종료입니다.
버튼 할당 중에는 해당 화면의 안내를 따르세요. START와 SELECT도 할당할 수 있습니다.
이미 저장한 컨트롤러 설정은 이후 다른 메뉴 변경을 취소해도 유지됩니다.

Lane options (배치 옵션)
------------------------

- NORMAL, MIRROR, RANDOM, S-RANDOM, H-RAN, ALL-SCR.
- R-RANDOM, Random Trainer.
- RANDOM+, S-RAN+, MIRROR+: 먼저 호환되는 기본 배치 옵션을 선택하세요.
- Trainer pattern: 7키 배치를 직접 지정하거나, 흰색/파란색 건반의 도착 위치를 지정하고
  각 색상 그룹 안에서 랜덤을 적용합니다.
- Field: DP에서 왼쪽 또는 오른쪽의 Trainer 배치를 선택합니다.

R-RANDOM과 Trainer는 7키 플레이용입니다.
이 빌드에서는 Trainer를 사용해도 일반 스코어와 클리어 램프를 저장할 수 있습니다.

Player options (플레이어 옵션)
------------------------------

- Gauge Auto Shift: 하드 게이지가 소진돼도 더 쉬운 게이지로 계속 플레이할 수 있습니다.
  RESULT에서 SELECT를 누르면 표시할 게이지 결과를 전환합니다.
- Floating Hi-Speed: FHS를 켭니다. Lock cover / START reset은 별도로 설정할 수 있습니다.
  조작법은 '플레이'를 참고하세요.
- LIFT: 아래쪽 커버와 판정선 올리기를 켭니다.

Adjustment timing (판정 타이밍 조정)
------------------------------------

메인 Options 화면으로 돌아가지 않고 판정 오프셋을 바꿀 수 있습니다.
배틀에서는 플레이어별로 설정하고, DP는 양쪽에 같은 값을 사용합니다.

Game options (게임 옵션)
------------------------

- REGUL SPEED: BPM에 관계없이 일정한 스크롤 속도. 일명 CMod입니다.
- GAMBOL TIMING: OFF, HYPER, ANOTHER.
- HAZARD: 한 번 틀리면 종료. HARD가 필요합니다.
- QUICK RETRY: '플레이'에 있는 빠른 재시도 조작을 켭니다.

분홍색 항목은 스코어 저장에 제한이 있거나 어시스트 클리어가 적용되는 설정입니다.
제한이 활성화되면 메뉴 밖에도 ASSIST ON이 표시됩니다.
CMD:에는 적용 중인 배치 옵션 또는 Trainer 배치가 나옵니다.
일부 기존 커맨드 옵션은 두 플레이어가 공유합니다.

Rebind controller (컨트롤러 재설정)
----------------------------------

이미 USB를 지정한 플레이어용입니다.

- Rebind buttons: 현재 할당을 수정합니다.
- Assign new controller: 기존 컨트롤러를 뽑고 새 컨트롤러를 연결한 뒤,
  종류 선택, 버튼 할당, 턴테이블 설정을 진행합니다.
- Change controller type: 할당을 지우지 않고 종류를 바꿉니다.

Customize play scene (플레이 화면 커스터마이즈)
----------------------------------------------

실시간 스킨 에디터를 엽니다. FAST/SLOW와 BPM 변속 알림도 여기서 설정합니다.

스킨 커스터마이즈
-----------------

선택한 레이아웃으로 오토플레이 미리보기 채보를 불러옵니다.
플레이 화면을 보면서 값을 바꾸고 저장하거나 취소하세요.

설정 가능한 항목:

- Lane light: 레인 밝기.
- Keybeam height: 키 빔 높이.
- Note height: 노트 높이.
- Judgement height: 판정 문자 높이.
- FAST/SLOW: OFF, TYPE A / B / C.
- F/S counters: 판정 개수 옆에 FAST/SLOW 누적 개수를 표시하거나 숨깁니다.
- BPM notice: OFF, TYPE A / B / C / D.
- Ghost: OFF, TYPE A / B / C.
- Frame: 기존 EMPRESS 프레임.
- Notes: EMPRESS 노트 이미지.
- Explosion art: RANDOM을 포함한 기존 EMPRESS 폭발 이펙트.
- Explosion size: 70~130%, 10% 단위. 기본 100%.
- Turntable: 턴테이블.
- Lane cover: 레인 커버.
- Play layout: 싱글과 DP의 Original / Arcade.
- DP type: Type A / B.

배틀에서는 P1과 P2 값이 나란히 표시됩니다. 해당 설정과 이미지는 플레이어별로 적용되고,
프레임/레이아웃은 공유합니다. DP는 한 플레이어의 설정을 양쪽 필드에 사용합니다.

지원하는 이미지는 미리보기를 멈추지 않고 바꿀 수 있습니다.
레이아웃을 변경하면 미리보기를 다시 불러옵니다.
Original DP는 BGA 영역 대신 화면 중앙의 오버레이를 사용합니다.

플레이
------

Floating Hi-Speed (FHS)
-----------------------

Player options에서 FHS를 켠 뒤 START를 누른 채 SELECT를 짧게 눌렀다 놓으면
일반 하이스피드와 플로팅 하이스피드를 전환합니다.

Lock cover / START reset이 켜져 있으면 READY가 나올 때 커버 위치가 고정됩니다.
플레이 중 START를 빠르게 두 번 누르면 SUDDEN+를 켜고 끄는 대신,
현재 BPM에서 목표 녹숫에 맞게 배속을 다시 맞춥니다.
READY 전에는 기존 커버 조작을 그대로 사용할 수 있습니다.
START + SELECT를 길게 눌러 종료하는 조작도 그대로 됩니다.

LIFT
----

START를 누른 채 턴테이블을 돌려 커버를 조절합니다.
SUDDEN+가 보이면 SUDDEN+를 조절합니다. START를 빠르게 두 번 눌러 SUDDEN+를 끄고,
LIFT를 조절한 뒤 다시 두 번 눌러 SUDDEN+를 켜세요.
FHS 커버 고정을 사용한다면 둘 다 READY 전에 맞추세요.

재시도와 RANDOM 배치 유지
------------------------

Quick Retry가 켜져 있으면 종료 페이드 또는 STAGE FAILED 연출 중에 누르세요.

- START: 새 RANDOM 배치로 재시도.
- SELECT: 이전 배치를 유지해 재시도.

RESULT에서 SELECT를 누른 채 CONTINUE를 선택하고 화면 전환 동안에도 계속 누르면
이전 배치를 유지합니다. SELECT 없이 CONTINUE를 선택하면 새 배치가 나옵니다.

세이브
------

MEGAMIX는 기존 곡 수 제한이 없는 압축 세이브 형식을 사용합니다.
스코어는 슬롯 1의 메모리 카드에 저장합니다. 실물 메모리 카드와 MMCE의 메모리 카드 기능
모두 사용할 수 있습니다. ELF와 게임 파일 위치는 스코어 저장 위치에 영향을 주지 않습니다.

MEGAMIX 세이브 없이 원본 EMPRESS 세이브만 있으면 그 데이터를 불러옵니다.
저장하면 MEGAMIX 데이터가 별도로 추가됩니다.
원본 세이브는 EMPRESS에서 그대로 쓸 수 있지만, 이후 MEGAMIX 진행 상황은 반영되지 않습니다.

호환되는 세이브가 없으면 게임 내에서 새 데이터를 만들고 수동 SAVE를 하세요.
PC 빌더는 스코어 세이브를 만들지 않습니다. 런처에서 플레이 후 저장을 선택했더라도
게임 내 AUTOSAVE를 켜야 합니다.

새 형식은 두 세대를 유지하므로 저장이 중단되면 이전 세대로 돌아갈 수 있습니다.
그래도 정상적인 카드는 필요합니다. 저장 중에 전원을 끄거나 카드를 빼지 마세요.
FAST/SLOW 카운터는 표시용이며 저장되지 않습니다.

런처와 MOD MENU 설정은 스코어 세이브와 별개입니다.
Game ID를 지원하는 MMCE 장치에서는 기본값으로 PIZD-00000을 사용합니다.
원본 세이브를 가져오려면 해당 가상 카드에 넣어 주세요. 다른 VMC를 자동으로 찾지는 않습니다.

감사의 말
---------

다음 프로젝트에 참여한 분들께 감사드립니다.

- bluric의 480p 패치 (https://github.com/blurtic2/iidx-ps2-480p):
  이 프로젝트를 시작하게 된 계기.
- 95jack의 EMPRESS CS FAST/SLOW 패치 (https://www.youtube.com/watch?v=anNfo8uBubE).
- PS2SDK / ps2dev (https://github.com/ps2dev/ps2sdk): SDK와 드라이버.
- gsKit (https://github.com/ps2dev/gsKit): 런처 그래픽.
- MMCEMAN (https://github.com/ps2-mmce/mmceman): MMCE 지원.
- ChaN의 FatFs (https://elm-chan.org/fsw/ff/): 파일시스템 지원.
- ps2-packer (https://github.com/ps2dev/ps2-packer): ELF 압축.
- aixxe의 2dx-gsm (https://github.com/aixxe/2dx-gsm): Gauge Auto Shift 참고.
- SaxxonPike의 iidx-ps2tools (https://github.com/SaxxonPike/iidx-ps2tools):
  PS2 데이터 형식과 변환 도구.
- Glebsin의 iidx2bms (https://github.com/Glebsin/iidx2bms): 채보 형식 참고.
- xdelta (https://github.com/jmacd/xdelta): 바이너리 델타 레시피.
- Python (https://www.python.org/), Pillow (https://python-pillow.org/),
  NumPy (https://numpy.org/): 개발 및 애셋 생성 도구. 빌더 실행에는 Python만 필요합니다.

조사와 테스트에는 다음 프로젝트도 사용했습니다.
PCSX2 (https://github.com/PCSX2/pcsx2),
Ghidra (https://github.com/NationalSecurityAgency/ghidra),
Open PS2 Loader (https://github.com/ps2homebrew/Open-PS2-Loader),
OpenLR2 (https://github.com/GOMazk/OpenLR2),
beatoraja (https://github.com/exch-bms2/beatoraja),
lr2oraja-endlessdream (https://github.com/seraxis/lr2oraja-endlessdream).

여기에 나열된 프로젝트가 모두 동봉되는 것은 아닙니다.
동봉된 라이선스 고지와 저작자 표시는 licenses 폴더를 확인하세요.
