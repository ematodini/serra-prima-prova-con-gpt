window.addEventListener('load', () => {
  const tempEl = document.getElementById('temp'); // ora .value inside .sensor.temp
  const humEl = document.getElementById('hum');   // ora .value inside .sensor.hum
  const startBtn = document.getElementById('start');
  const startWrap = document.getElementById('startWrap');

  const timeBtn = document.getElementById('timeBtn');
  const intervalBtn = document.getElementById('intervalBtn');
  const msgEl = document.getElementById('msg');

  const nextText = document.getElementById('nextText');
  const progressBar = document.getElementById('progressBar');
  // removed nextBadge usage (badge removed from HTML)

  // modal tempo
  const timeModal = document.getElementById('timeModal');
  const timeInputModal = document.getElementById('timeInputModal');
  const timeOk = document.getElementById('timeOk');
  const timeCancel = document.getElementById('timeCancel');

  // modal intervallo
  const intervalModal = document.getElementById('intervalModal');
  const intervalInputModal = document.getElementById('intervalInputModal');
  const intervalOk = document.getElementById('intervalOk');
  const intervalCancel = document.getElementById('intervalCancel');

  if (!tempEl || !humEl || !startBtn || !timeBtn || !intervalBtn || !startWrap) {
    console.error('Missing DOM elements in app.js — aborting JS init');
    return;
  }

  let timeModalOpen = false;
  let intervalModalOpen = false;

  // valori precedenti per rilevare aggiornamenti
  let prevTemp = null;
  let prevHum = null;

  function markUpdated(el) {
    if (!el) return;
    el.classList.add('updated');
    clearTimeout(el._updTimeout);
    el._updTimeout = setTimeout(()=>{ el.classList.remove('updated'); }, 800);
  }

  function fmtHMS(seconds){
    seconds = Math.max(0, Math.floor(Number(seconds) || 0));
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    const hh = String(h).padStart(2,'0');
    const mm = String(m).padStart(2,'0');
    const ss = String(s).padStart(2,'0');
    return hh + ':' + mm + ':' + ss;
  }

  async function fetchStatus(){
    try{
      let r = await fetch('/status');
      let j = await r.json();

      // temperatura: confronta con una cifra decimale
      const newTemp = (Number.isFinite(j.temperature) ? Number(j.temperature).toFixed(1) : null);
      if (newTemp !== null) {
        if (prevTemp === null || String(prevTemp) !== String(newTemp)) {
          tempEl.textContent = newTemp + '°C';
          markUpdated(tempEl);
        } else {
          tempEl.textContent = newTemp + '°C';
        }
        prevTemp = newTemp;
      } else {
        tempEl.textContent = '--°C';
      }

      // umidità: confronto su intero
      const newHum = (Number.isFinite(j.humidity) ? Math.round(j.humidity) : null);
      if (newHum !== null) {
        if (prevHum === null || Number(prevHum) !== Number(newHum)) {
          humEl.textContent = newHum + '%';
          markUpdated(humEl);
        } else {
          humEl.textContent = newHum + '%';
        }
        prevHum = newHum;
      } else {
        humEl.textContent = '--%';
      }

      if (!timeModalOpen) {
        const s = Math.max(1, Math.round(j.irrigation_time/1000));
        timeBtn.textContent = s + ' s';
      }
      if (!intervalModalOpen) {
        const i = Math.max(1, Math.round(j.irrigation_interval/60000));
        intervalBtn.textContent = i + ' min';
      }
      if (j.irrigation) {
        startBtn.classList.add('active');
        startBtn.disabled = true;
        startBtn.textContent = 'Irrigazione...';
        startWrap.classList.add('active');
      } else {
        startBtn.classList.remove('active');
        startBtn.disabled = false;
        startBtn.textContent = 'Irrigare';
        startWrap.classList.remove('active');
      }

      // countdown: j.next_in è secondi rimanenti
      const nextIn = Number.isFinite(j.next_in) ? Number(j.next_in) : 0;
      nextText.textContent = 'Prossima irrigazione tra ' + fmtHMS(nextIn);
      // progress: barra parte piena e si riduce verso centro (usa percentuale rimanente)
      const intervalSec = Math.max(1, Math.round(j.irrigation_interval/1000));
      let pct = 0;
      if (intervalSec > 0) {
        pct = Math.min(100, Math.max(0, Math.round((nextIn / intervalSec) * 100)));
      }
      progressBar.style.width = pct + '%';

    } catch(e){
      tempEl.textContent = '--°C';
      humEl.textContent = '--%';
      nextText.textContent = 'Prossima irrigazione tra --:--:--';
      progressBar.style.width = '0%';
    }
  }

  /* TIME modal handlers (restano in secondi) */
  timeBtn.addEventListener('click', () => {
    timeModalOpen = true;
    timeModal.classList.add('open');
    timeModal.removeAttribute('hidden');
    timeModal.setAttribute('aria-hidden','false');
    const cur = parseInt((timeBtn.textContent || '5').replace(/\D/g,''),10) || 5;
    timeInputModal.value = cur;
    setTimeout(()=>{ timeInputModal.focus(); timeInputModal.select(); }, 50);
  });
  timeCancel.addEventListener('click', () => {
    timeModalOpen = false;
    timeModal.classList.remove('open');
    timeModal.setAttribute('hidden','');
    timeModal.setAttribute('aria-hidden','true');
    fetchStatus();
  });
  timeOk.addEventListener('click', async () => {
    let t = parseInt(timeInputModal.value,10) || 5;
    if (t < 1) t = 1;
    if (t > 3600) t = 3600;
    await fetch('/set-time', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'time=' + encodeURIComponent(t)
    }).catch(()=>{/*ignore*/});
    msgEl.textContent = 'Tempo salvato';
    setTimeout(()=>{ msgEl.textContent = ''; }, 1500);
    timeModalOpen = false;
    timeModal.classList.remove('open');
    timeModal.setAttribute('hidden','');
    timeModal.setAttribute('aria-hidden','true');
    timeBtn.textContent = t + ' s';
    fetchStatus();
  });

  /* INTERVAL modal handlers (minuti) */
  intervalBtn.addEventListener('click', () => {
    intervalModalOpen = true;
    intervalModal.classList.add('open');
    intervalModal.removeAttribute('hidden');
    intervalModal.setAttribute('aria-hidden','false');
    const cur = parseInt((intervalBtn.textContent || '5').replace(/\D/g,''),10) || 5;
    intervalInputModal.value = cur;
    setTimeout(()=>{ intervalInputModal.focus(); intervalInputModal.select(); }, 50);
  });
  intervalCancel.addEventListener('click', () => {
    intervalModalOpen = false;
    intervalModal.classList.remove('open');
    intervalModal.setAttribute('hidden','');
    intervalModal.setAttribute('aria-hidden','true');
    fetchStatus();
  });
  intervalOk.addEventListener('click', async () => {
    let t = parseInt(intervalInputModal.value,10) || 5; // t in minuti
    if (t < 1) t = 1;
    if (t > 1440) t = 1440;
    await fetch('/set-interval', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'interval=' + encodeURIComponent(t) // invia minuti
    }).catch(()=>{/*ignore*/});
    msgEl.textContent = 'Intervallo salvato';
    setTimeout(()=>{ msgEl.textContent = ''; }, 1500);
    intervalModalOpen = false;
    intervalModal.classList.remove('open');
    intervalModal.setAttribute('hidden','');
    intervalModal.setAttribute('aria-hidden','true');
    intervalBtn.textContent = t + ' min';
    fetchStatus();
  });

  /* modal close shortcuts */
  timeModal.addEventListener('click', (e) => { if (e.target.classList.contains('modal-backdrop')) timeCancel.click(); });
  intervalModal.addEventListener('click', (e) => { if (e.target.classList.contains('modal-backdrop')) intervalCancel.click(); });
  document.addEventListener('keydown', (e) => {
    if (timeModalOpen && e.key === 'Escape') timeCancel.click();
    if (intervalModalOpen && e.key === 'Escape') intervalCancel.click();
    if (timeModalOpen && e.key === 'Enter') timeOk.click();
    if (intervalModalOpen && e.key === 'Enter') intervalOk.click();
  });

  /* start button: avvia irrigazione aggiuntiva (sovrascrive la corrente) */
  startBtn.addEventListener('click', async () => {
    startBtn.disabled = true;
    startBtn.classList.add('active');
    startWrap.classList.add('active');
    startBtn.textContent = 'Irrigazione...';
    await fetch('/start', { method: 'POST' }).catch(()=>{/*ignore*/});
    msgEl.textContent = 'Irrigazione avviata';
    setTimeout(()=>{ msgEl.textContent = ''; }, 1500);
    fetchStatus();
  });

  /* periodic update */
  setInterval(fetchStatus, 1000);
  fetchStatus();
});