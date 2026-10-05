// Slot (QK4 Button only) is the action's saved param; the port is a global setting shared by every action.
const form = document.getElementById('settings');
const actionUuid = new URLSearchParams(location.search).get('uuid') || '';
// Show the slot unless we know this is not a QK4 Button (an empty uuid shows it rather than hide it wrongly).
document.getElementById('slot-row').hidden = actionUuid !== '' && !actionUuid.endsWith('.button');

function showSlot(param) {
  if (param && param.slot) form.slot.value = String(param.slot);
}

$UD.connect();
$UD.onConnected(() => {
  $UD.getGlobalSettings();
  form.slot.addEventListener('change', () => $UD.sendParamFromPlugin({ slot: Number(form.slot.value) }));
  form.port.addEventListener('change', () => {
    const port = Number(form.port.value);
    if (Number.isInteger(port) && port >= 1024 && port <= 65535) $UD.setGlobalSettings({ port });
  });
});
$UD.onAdd((jsn) => showSlot(jsn.param));
$UD.onParamFromApp((jsn) => showSlot(jsn.param));
$UD.onDidReceiveGlobalSettings((jsn) => {
  if (jsn.settings && jsn.settings.port) form.port.value = String(jsn.settings.port);
});
