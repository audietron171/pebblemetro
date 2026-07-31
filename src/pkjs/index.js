/* global Pebble, XMLHttpRequest, window, localStorage */

// Data queue
var backlog = []

// Auth
const devId = '<insert_here>'
const apiKey = '<insert_here>'

function healthCheck() {
  // Mock search (no health endpoint on V3 API)
  const query = '/v3/search/sample'
  makeApiRequest(query, (req) => {
    // Wait for request to finish
    if (req.readyState !== 4)
      return

    // Check if request successful
    let success = false
    if (req.status === 200) {
      success = true
    }

    // Provide to watch
    console.log("Health check: " + success)
    Pebble.sendAppMessage({
      'PTV_HEALTH_KEY': true,
    });
  })
}

function makeApiRequest(query, responseCallback) {
  // Request data
  function requestData(url, responseCallback) {
    var req = new XMLHttpRequest();
    req.open('GET', url, true);
    req.onload = function () {
      console.log(`Request completed`)
      responseCallback(req)
    }
    req.send(null);
  }

  function buildRequest(request, responseCallback){
    // Add devid
    if (!request.match(/\/.*\?/)) request += '?'
    request += '&devid=' + devId

    // Create signature for API
    var enc = new TextEncoder("utf-8");
    window.crypto.subtle.importKey(
      "raw", // raw format of the key - should be Uint8Array
      enc.encode(apiKey),
      { // algorithm details
        name: "HMAC",
        hash: { name: "SHA-1" }
      },
      false, // export = false
      ["sign", "verify"] // what this key can do
    ).then(key => {
      window.crypto.subtle.sign(
        "HMAC",
        key,
        enc.encode(request)
      ).then(signature => {
        // Convert signuature to a string
        var b = new Uint8Array(signature);
        var str = Array.prototype.map.call(b, x => x.toString(16).padStart(2, '0')).join("")

        // Add to query and make request
        const url = 'https://timetableapi.ptv.vic.gov.au' + request + '&signature=' + str.toString()
        console.log("Using url: " + url)
        requestData(url, responseCallback)
      });
    })
  }

  // Sign and add auth for request
  buildRequest(query, responseCallback)
}

// Convert a datetime string to 24hr time
/*
function convertTimeTo24(date) {
  const time = new Date(date)
  var hours = time.getHours()

  // Add leading zero to minutes where required
  var minutes = time.getMinutes()
  if (minutes < 10)
    minutes = '0' + minutes

  return hours + ':' + minutes
}
*/

// Convert a datetime string to 12hr time
function convertTimeTo12(date) {
  const time = new Date(date)
  var meridiem = 'am'

  // Convert 18hrs -> 6pm
  var hours = time.getHours()
  if (hours > 12) {
    hours = hours % 12
    meridiem = 'pm'
  }

  // Add leading zero to minutes where required
  var minutes = time.getMinutes()
  if (minutes < 10)
    minutes = '0' + minutes

  return hours + ':' + minutes + meridiem
}

function getStopData(type, station, direction = null) {
    // Query for departures given request info from watch
    var query = '/v3/departures/route_type/' + type + '/stop/' + station + '?max_results=3&expand=run';
    if (direction) query += '&direction_id=' + direction

    // Make request
    makeApiRequest(query, (req) => {
      // Wait for request to complete
      if (req.readyState !== 4)
        return
      if (req.status !== 200){
        console.log('Request failed to be completed:' + JSON.stringify(req));
      }

      // Parse request
      const response = JSON.parse(req.responseText)
      const departures = response.departures
      if (!departures.length) {
        console.log('Warning, recieved no depatures for direction ')
        return
      }

      // Retrieve departure destinations and parse estimated arrival time (to 12/24hr time)
      var depatureDests = []
      var departureTimes = []
      for (var i = 0; i < departures.length; i++) {
        // Retrieve time
        var time = null
        if (departures[i].estimated_departure_utc)
          time = departures[i].estimated_departure_utc
        else
          time = departures[i].scheduled_departure_utc
        departureTimes.push(convertTimeTo12(time))

        // Retrieve destination (and truncate if too long for watch)
        const runs = response.runs
        var dest = runs[departures[i].run_ref].destination_name
        if (dest.length > 15) {
          dest = dest.slice(0, 14) + '...'
        }
        depatureDests.push(dest)
      }

      // Retrieve mins to next departure (ideally actual estimate)
      var nextDepartureTime = convertTimeTo12(departures[0].scheduled_departure_utc)
      if (departures[0].estimated_departure_utc) {
        // Retrieve mins to departure
        const timeToDepart = Math.round(((new Date(departures[0].estimated_departure_utc).getTime()) - Date.now()) / (60 * 1000))

        // Return "now" if departing in 0mins
        if (timeToDepart == 0) nextDepartureTime = `Now`
        else if (timeToDepart < 59) nextDepartureTime = `${timeToDepart}mins`
      }

      // Send data seperately to avoid hitting inbox limit
      backlog.push({ 'PTV_NEXT_TIME_KEY': nextDepartureTime })
      backlog.push({
        'PTV_1_DEST_KEY': depatureDests[0],
        'PTV_1_TIME_KEY': departureTimes[0]
      })
      backlog.push({
        'PTV_2_DEST_KEY': depatureDests[1],
        'PTV_2_TIME_KEY': departureTimes[1]
      })
      backlog.push({
        'PTV_3_DEST_KEY': depatureDests[2],
        'PTV_3_TIME_KEY': departureTimes[2]
      })
      sendData()
      return
    })
}

// Send data using a queue
function sendData() {
  if (!backlog.length)
    return

  // Retrieve next data
  const send = backlog.shift()
  
  // Indicate when data finished
  let ack = 1
  if (!backlog.length) ack = 0
  send['DATA_ACK'] = ack

  console.log('Data remaining: ' + backlog.length)
  Pebble.sendAppMessage(send, function () { console.log('Sent data successfully') }, function (e) { console.log(`Failed to send:` + e) })
}

// Fetch nearby departures using device GPS and send results to watch
function getNearbyDepartures() {
  if (!navigator.geolocation) {
    console.log('Geolocation not available')
    return
  }

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      var lat = pos.coords.latitude
      var lng = pos.coords.longitude
      console.log('Got location: ' + lat + ',' + lng)

      var query = '/v3/stops/location/' + lat + ',' + lng + '?max_results=5'
      makeApiRequest(query, function (req) {
        if (req.readyState !== 4) return
        if (req.status !== 200) {
          console.log('Nearby stops request failed: ' + req.status)
          return
        }
        var data = JSON.parse(req.responseText)
        var stops = data.stops || []
        console.log('Found ' + stops.length + ' nearby stops')
        fetchDeparturesSequentially(stops, 0)
      })
    },
    function (err) {
      console.log('Geolocation error: ' + err.message)
    },
    { timeout: 15000, maximumAge: 60000 }
  )
}

// Sequentially fetch one departure per stop and queue results for sending
function fetchDeparturesSequentially(stops, idx) {
  if (idx >= stops.length || idx >= 8) {
    sendData()
    return
  }

  var stop = stops[idx]
  var routeType = stop.route_type
  var stopId = stop.stop_id
  var query = '/v3/departures/route_type/' + routeType + '/stop/' + stopId + '?max_results=1&expand=run'

  makeApiRequest(query, function (req) {
    if (req.readyState !== 4 || req.status !== 200) {
      fetchDeparturesSequentially(stops, idx + 1)
      return
    }

    try {
      var data = JSON.parse(req.responseText)
      var departures = data.departures || []

      if (departures.length > 0) {
        var dep = departures[0]
        var timeUtc = dep.estimated_departure_utc || dep.scheduled_departure_utc
        var timeStr = convertTimeTo12(timeUtc)

        if (dep.estimated_departure_utc) {
          var mins = Math.round(
            (new Date(dep.estimated_departure_utc).getTime() - Date.now()) / 60000
          )
          if (mins <= 0) timeStr = 'Now'
          else if (mins < 60) timeStr = mins + 'min'
        }

        var dest = ''
        if (data.runs && dep.run_ref && data.runs[dep.run_ref]) {
          dest = data.runs[dep.run_ref].destination_name || ''
        }

        var stopName = stop.stop_name || 'Unknown'
        if (stopName.length > 20) stopName = stopName.slice(0, 19) + '.'
        if (dest.length > 20) dest = dest.slice(0, 19) + '.'

        backlog.push({
          'NEARBY_STOP_NAME_KEY': stopName,
          'NEARBY_DEST_KEY': dest,
          'NEARBY_TIME_KEY': timeStr,
          'NEARBY_ENTRY_INDEX_KEY': idx
        })
      }
    } catch (e) {
      console.log('Error parsing departure for stop ' + idx + ': ' + e)
    }

    fetchDeparturesSequentially(stops, idx + 1)
  })
}

// Perform a health check on initial boot
Pebble.addEventListener('ready', function (e) {
  console.log('connect!' + e.ready);
  console.log(e.type);
  healthCheck()
});

Pebble.addEventListener('appmessage', function (e) {
  var dictionary = e.payload;
  console.log('Got message: ' + JSON.stringify(dictionary));

  // Process request for nearby departures
  if (dictionary.PTV_REQ_NEARBY != undefined) {
    backlog = []
    getNearbyDepartures()
    return
  }

  // Process new request for a configured stop
  if (dictionary.PTV_REQ_STOP_NUMBER != undefined) {
    // New request so drop old data
    backlog = []

    // Send request
    const typeNumber = localStorage.getItem('SETTINGS_STOP_' + dictionary.PTV_REQ_STOP_NUMBER + '_TYPE')
    const stationId = localStorage.getItem('SETTINGS_STOP_' + dictionary.PTV_REQ_STOP_NUMBER + '_STOP')
    getStopData(typeNumber, stationId)
    return
  }

  // Watch recieved data, send more if available
  if (dictionary.DATA_ACK == 0) {
    // Watch receieved data, send more data if available
    console.log(`Recieved ack`)
    sendData()
    return
  }
});

/******************************************************************************************************** */

// Clay

var Clay = require('pebble-clay');
var clayConfig = require('./config.json');
var customClay = require('./custom-clay');

// Handling Clay events ourselves (don't need to send all options to the watch)
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });
Pebble.addEventListener('showConfiguration', function (_e) {
  Pebble.openURL(clay.generateUrl())
});
Pebble.addEventListener('webviewclosed', function (e) {
  console.log('webview closed');
  console.log(e.type);
  console.log(e.response);

  // Didn't hit save, ignore
  if (!e.response)
    return

  // Save settings to local storage
  let watchSettings = {}
  let storedSettings = clay.getSettings(e.response, false)
  for (const setting in storedSettings) {
    // Determine value
    let value = null
    if (storedSettings[setting].value)
      value = storedSettings[setting].value

    // Save value to localstorage (for Pebblekit)
    localStorage.setItem(setting, value);

    // Manually send intested settings to watch
    if ((setting.includes('_NAME') || setting.includes('_TYPE')) && value != null) {
      // Must convert to integer
      if (setting.includes('_TYPE'))
        value = parseInt(value)

      watchSettings[setting] = value
    }
  }

  // Send only the required settings to the watch
  Pebble.sendAppMessage(watchSettings, function (_e) {
    console.log('Sent config data to Pebble');
  }, function (e) {
    console.log('Failed to send config data!');
    console.log(JSON.stringify(e));
  });
});