let writer;
const container = document.getElementById("container");

// Ampero II Stomp 전용 MIDI 컨트롤 명령 매핑 리스트
const amperoCommands = [
    { name: "PC (패치 번호 직접 지정)", value: "PC" },
    { name: "Bank - (뱅크 다운)", value: "22" },
    { name: "Bank + (뱅크 업)", value: "23" },
    { name: "Patch - (패치 다운)", value: "26" },
    { name: "Patch + (패치 업)", value: "27" },
    { name: "Tuner On/Off (튜너)", value: "60" },
    { name: "Tap Tempo (탭 템포)", value: "76" },
    { name: "FS1 이펙트 온/오프", value: "79" },
    { name: "FS2 이펙트 온/오프", value: "80" },
    { name: "FS3 이펙트 온/오프", value: "81" }
];

function createUI() {
    for (let mode = 0; mode < 3; mode++) {
        const div = document.createElement("div");
        div.className = "mode";
        div.innerHTML = `<h2>Mode ${mode + 1}</h2>`;

        for (let btn = 0; btn < 3; btn++) {
            let optionsHtml = amperoCommands.map(cmd => 
                `<option value="${cmd.value}">${cmd.name}</option>`
            ).join('');

            div.innerHTML += `
              <div class="button-config" style="margin-bottom: 15px; border-bottom: 1px dashed #eee; padding-bottom: 10px;">
                  <strong>Button ${btn + 1}</strong><br>
                  할당 기능: 
                  <select id="type_${mode}_${btn}" onchange="toggleNumberInput(${mode}, ${btn})">
                      ${optionsHtml}
                  </select>
                  
                  <span id="num_container_${mode}_${btn}">
                      &nbsp;&nbsp;패치 번호: 
                      <input type="number" min="0" max="127" value="0" id="num_${mode}_${btn}" style="width: 60px;">
                  </span>
              </div>
            `;
        }
        container.appendChild(div);
    }
}

// PC 모드일 때만 번호 입력 칸이 보이게 제어하는 센스 구현
window.toggleNumberInput = function(mode, btn) {
    const selectVal = document.getElementById(`type_${mode}_${btn}`).value;
    const numContainer = document.getElementById(`num_container_${mode}_${btn}`);
    if (selectVal === "PC") {
        numContainer.style.display = "inline";
    } else {
        numContainer.style.display = "none";
    }
}

createUI();

// 초기 상태 빌드 시 인풋 박스 숨김 정돈
for(let m=0; m<3; m++) {
    for(let b=0; b<3; b++) toggleNumberInput(m, b);
}

document.getElementById("connectBtn").onclick = connect;
async function connect() {
    try {
        const port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });
        writer = port.writable.getWriter();
        alert("UNO 연결 완료");
    } catch (err) {
        alert("연결 실패: " + err.message);
    }
}

document.getElementById("saveBtn").onclick = saveConfig;
async function saveConfig() {
    if (!writer) {
        alert("UNO 먼저 연결");
        return;
    }

    const data = [];
    for (let mode = 0; mode < 3; mode++) {
        for (let btn = 0; btn < 3; btn++) {
            const selectVal = document.getElementById(`type_${mode}_${btn}`).value;
            let type = 1; // 기본은 CC 모드
            let number = 0;

            if (selectVal === "PC") {
                type = 0; // PC 모드 설정
                number = Number(document.getElementById(`num_${mode}_${btn}`).value);
            } else {
                number = Number(selectVal); // 22, 23, 60 같은 CC 번호 대입
            }

            data.push({ mode, button: btn, type, number });
        }
    }

    const payload = JSON.stringify(data) + "\n";
    
    try {
        const encoder = new TextEncoder();
        const encodedData = encoder.encode(payload);
        for (let i = 0; i < encodedData.length; i++) {
            await writer.write(new Uint8Array([encodedData[i]]));
            await new Promise(resolve => setTimeout(resolve, 2)); 
        }
        alert("Ampero II Stomp 맞춤 설정 저장 완료!");
    } catch (err) {
        alert("전송 오류: " + err.message);
    }
}