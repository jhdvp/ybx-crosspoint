<footer class="site-footer">
  <div class="footer-inner">
    <div class="footer-main">
      <div class="footer-brand"><a class="brand-link" href="/"><img class="brand-logo" src="/assets/crosspoint-icon.svg" alt="" width="28" height="28" />YBX <span class="brand-reader">Reader</span></a><p>Your books on your reader. YBX firmware for Xteink X3 and X4, based on CrossPoint Reader.</p></div>
      <nav class="footer-project"><span class="footer-label">Project</span><a href="https://ybx.app/" target="_blank" rel="noopener noreferrer">Website</a><a href="https://github.com/jhdvp/ybx-crosspoint" target="_blank" rel="noopener noreferrer">GitHub</a></nav>
    </div>
    <div class="footer-bottom"><span>YBX is based on CrossPoint Reader. Original authors and MIT license are preserved.</span><span>YBX · Your Books, eXtended.</span></div>
  </div>
</footer>
<dialog id="webMessageDialog" class="web-dialog" aria-labelledby="webDialogTitle" aria-describedby="webDialogMessage">
  <form method="dialog">
    <h2 id="webDialogTitle"></h2>
    <p id="webDialogMessage"></p>
    <div class="web-dialog-actions">
      <button id="webDialogCancel" value="cancel">Cancel</button>
      <button id="webDialogConfirm" value="confirm">OK</button>
    </div>
  </form>
</dialog>
<div id="webToast" class="toast" role="status" aria-live="polite"></div>
<script>
const webMessageDialog = document.getElementById('webMessageDialog');
const webDialogTitle = document.getElementById('webDialogTitle');
const webDialogMessage = document.getElementById('webDialogMessage');
const webDialogCancel = document.getElementById('webDialogCancel');
const webDialogConfirm = document.getElementById('webDialogConfirm');
let webDialogQueue = Promise.resolve();

function showWebDialog(title, message, confirm) {
  const result = webDialogQueue.then(() => new Promise(resolve => {
    webDialogTitle.textContent = title;
    webDialogMessage.textContent = String(message);
    webDialogCancel.hidden = !confirm;
    webDialogConfirm.textContent = confirm ? 'Confirm' : 'OK';
    webMessageDialog.returnValue = '';
    webMessageDialog.addEventListener('close', () => {
      resolve(webMessageDialog.returnValue === 'confirm');
    }, { once: true });
    webMessageDialog.showModal();
  }));
  webDialogQueue = result.then(() => undefined, () => undefined);
  return result;
}

function showWebAlert(message) {
  return showWebDialog('Notice', message, false).then(() => undefined);
}

function showWebConfirm(message) {
  return showWebDialog('Confirm action', message, true);
}

const webToast = document.getElementById('webToast');
let webToastTimeout;

function showWebToast(message, isError) {
  clearTimeout(webToastTimeout);
  webToast.textContent = message;
  webToast.className = 'toast' + (isError ? ' error' : '');
  webToast.style.display = 'block';
  webToastTimeout = setTimeout(() => { webToast.style.display = 'none'; }, 4000);
}
</script>
