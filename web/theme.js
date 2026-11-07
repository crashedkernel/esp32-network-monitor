(() => {
  'use strict';
  const root = document.documentElement;
  let savedTheme, savedSize;
  try {
    savedTheme = localStorage.getItem('nm-theme');
    savedSize = localStorage.getItem('nm-font-size')
  } catch (e) {}
  const preferred = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  let fontSize = 100;

  function applyTheme(theme) {
    root.dataset.theme = theme;
    for (const b of document.querySelectorAll('[data-theme-toggle]')) {
      b.setAttribute('aria-pressed', String(theme === 'dark'));
      b.setAttribute('aria-label', theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode');
      const label = b.querySelector('[data-theme-label]');
      if (label) label.textContent = theme === 'dark' ? 'Light mode' : 'Dark mode';
    }
  }

  function applySize(value) {
    const number = Number(value);
    fontSize = Number.isFinite(number) && number >= 80 && number <= 150 ? Math.round(number / 10) * 10 : 100;
    root.style.fontSize = (16 * fontSize / 100) + 'px';
    for (const label of document.querySelectorAll('[data-font-label]')) label.textContent = fontSize + '%';
    for (const b of document.querySelectorAll('[data-font-step]')) b.disabled = Number(b.dataset.fontStep) < 0 ? fontSize <= 80 : fontSize >= 150;
  }

  function saveSize(value) {
    applySize(value);
    try {
      localStorage.setItem('nm-font-size', String(fontSize))
    } catch (e) {}
  }
  applyTheme(savedTheme === 'dark' || savedTheme === 'light' ? savedTheme : preferred);
  applySize(savedSize);
  document.addEventListener('DOMContentLoaded', () => {
    applyTheme(root.dataset.theme);
    applySize(fontSize);
    for (const b of document.querySelectorAll('[data-theme-toggle]')) b.addEventListener('click', () => {
      const theme = root.dataset.theme === 'dark' ? 'light' : 'dark';
      try {
        localStorage.setItem('nm-theme', theme)
      } catch (e) {}
      applyTheme(theme);
    });
    for (const b of document.querySelectorAll('[data-font-step]')) b.addEventListener('click', () => saveSize(Math.max(80, Math.min(150, fontSize + Number(b.dataset.fontStep) * 10))));
    for (const b of document.querySelectorAll('[data-font-reset]')) b.addEventListener('click', () => saveSize(100));
  });
  window.addEventListener('storage', e => {
    if (e.key === 'nm-theme') applyTheme(e.newValue === 'dark' || e.newValue === 'light' ? e.newValue : preferred);
    if (e.key === 'nm-font-size') applySize(e.newValue);
  });
})();
