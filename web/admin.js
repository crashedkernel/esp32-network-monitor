(() => {
  'use strict';
  const $ = id => document.getElementById(id);

  function addressFields() {
    $('subnetFields').hidden = $('mode').value !== 'subnet';
    $('rangeFields').hidden = $('mode').value !== 'range';
  }

  function timeFields() {
    const custom = $('timeServer').value === 'custom';
    $('customTimeServerField').hidden = !custom;
    const input = document.querySelector('[name=customTimeServer]');
    input.disabled = !custom;
    input.required = custom;
    try {
      const preview = new Intl.DateTimeFormat(undefined, {
        timeZone: $('timeZone').value,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hourCycle: $('timeFormat').value === '12' ? 'h12' : 'h23',
        timeZoneName: 'short'
      });
      $('timePreview').textContent = 'Format preview: ' + preview.format(new Date());
    } catch (e) {
      $('timePreview').textContent = 'Choose a time zone supported by this browser.'
    }
  }
  $('source').addEventListener('change', () => {
    const gl = $('source').value === 'glinet';
    $('transport').value = gl ? 'ssh' : 'http';
    document.querySelector('[name=routerPort]').value = gl ? 22 : 80
  });
  $('mode').addEventListener('change', addressFields);
  addressFields();
  $('transport').addEventListener('change', function() {
    document.querySelector('[name=routerPort]').value = this.value === 'ssh' ? 22 : 80
  });
  for (const id of ['timeZone', 'timeFormat', 'timeServer']) $(id).addEventListener('change', timeFields);
  timeFields();
  (async () => {
    try {
      const response = await fetch('/assets/timezones.json');
      if (!response.ok) return;
      const zones = await response.json(),
        selected = $('timeZone').value;
      for (const zone of zones) {
        if (zone === selected) continue;
        try {
          new Intl.DateTimeFormat(undefined, {
            timeZone: zone
          })
        } catch (e) {
          continue
        }
        const option = document.createElement('option');
        option.value = zone;
        option.textContent = zone.replaceAll('_', ' ');
        $('timeZone').appendChild(option);
      }
    } catch (e) {}
  })();
  $('settingsForm').addEventListener('submit', () => {
    $('saveButton').disabled = true;
    $('saveButton').textContent = 'Saving settings...'
  });
  $('resetConfirmation').addEventListener('input', () => {
    $('resetButton').disabled = $('resetConfirmation').value !== 'RESET'
  });
  $('resetForm').addEventListener('submit', event => {
    if ($('resetConfirmation').value !== 'RESET') {
      event.preventDefault();
      return
    }
    $('resetButton').disabled = true;
    $('resetButton').textContent = 'Resetting...';
  });
})();
