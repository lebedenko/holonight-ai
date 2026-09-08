"""Each mutation must fail independently against the real application policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
checker = root / "scripts/check-canonical-qml-imports.sh"
with tempfile.TemporaryDirectory() as directory:
    fixture = Path(directory)
    shutil.copytree(root / "qml", fixture / "qml")
    def check(expected):
        result = subprocess.run(["bash", str(checker), str(fixture)], capture_output=True, text=True)
        assert (result.returncode == 0) == expected, result.stdout + result.stderr
    check(True)
    path = fixture / "qml/PolicyFixture.qml"
    for source in [
        "import Holonight as H\nH.Button {}",
        "import Holonight.Controls\nHnListDelegate { selectionStyle: HnSelectableDelegate.Outline }",
        "import QtQuick.Controls.Basic as B\nB.Button {}",
        "import QtQuick.Controls\nButton {}",
        "import QtQuick.Controls as Wrong\nWrong.Button {}",
        "import QtQuick.Controls as Controls\nButton {}",
        "import QtQuick.Controls as Controls\nItem { property int p: Popup.CloseOnEscape }",
        "import QtQuick.Controls as Controls\nItem { ScrollBar.vertical: Controls.ScrollBar {} }",
        "import QtQuick\nControls.Button {}",
    ]:
        path.write_text(source + "\n")
        check(False)
    path.unlink()
    for file, before, after in [
        ("shared/ChatHeader.qml", "HnIconComboBox {", "Item {"),
        ("workspace/ConversationListPanel.qml", "currentIndex: -1", "currentIndex: 0"),
    ]:
        target = fixture / "qml" / file
        original = target.read_text()
        assert before in original
        target.write_text(original.replace(before, after))
        check(False)
        target.write_text(original)
    check(True)
print("Independent runtime/adoption/selection policy fixtures passed")
