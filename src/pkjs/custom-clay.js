module.exports = function(minified) {
  var clayConfig = this;

  var buildUrl = function (query) {
    // HMAC SHA1 libs (no in-built libraries for this :( )
    /*
    CryptoJS v3.1.2
    code.google.com/p/crypto-js
    (c) 2009-2013 by Jeff Mott. All rights reserved.
    code.google.com/p/crypto-js/wiki/License
    */
    /* eslint-disable */
    var CryptoJS=CryptoJS||function(g,l){var e={},d=e.lib={},m=function(){},k=d.Base={extend:function(a){m.prototype=this;var c=new m;a&&c.mixIn(a);c.hasOwnProperty("init")||(c.init=function(){c.$super.init.apply(this,arguments)});c.init.prototype=c;c.$super=this;return c},create:function(){var a=this.extend();a.init.apply(a,arguments);return a},init:function(){},mixIn:function(a){for(var c in a)a.hasOwnProperty(c)&&(this[c]=a[c]);a.hasOwnProperty("toString")&&(this.toString=a.toString)},clone:function(){return this.init.prototype.extend(this)}},
    p=d.WordArray=k.extend({init:function(a,c){a=this.words=a||[];this.sigBytes=c!=l?c:4*a.length},toString:function(a){return(a||n).stringify(this)},concat:function(a){var c=this.words,q=a.words,f=this.sigBytes;a=a.sigBytes;this.clamp();if(f%4)for(var b=0;b<a;b++)c[f+b>>>2]|=(q[b>>>2]>>>24-8*(b%4)&255)<<24-8*((f+b)%4);else if(65535<q.length)for(b=0;b<a;b+=4)c[f+b>>>2]=q[b>>>2];else c.push.apply(c,q);this.sigBytes+=a;return this},clamp:function(){var a=this.words,c=this.sigBytes;a[c>>>2]&=4294967295<<
    32-8*(c%4);a.length=g.ceil(c/4)},clone:function(){var a=k.clone.call(this);a.words=this.words.slice(0);return a},random:function(a){for(var c=[],b=0;b<a;b+=4)c.push(4294967296*g.random()|0);return new p.init(c,a)}}),b=e.enc={},n=b.Hex={stringify:function(a){var c=a.words;a=a.sigBytes;for(var b=[],f=0;f<a;f++){var d=c[f>>>2]>>>24-8*(f%4)&255;b.push((d>>>4).toString(16));b.push((d&15).toString(16))}return b.join("")},parse:function(a){for(var c=a.length,b=[],f=0;f<c;f+=2)b[f>>>3]|=parseInt(a.substr(f,
    2),16)<<24-4*(f%8);return new p.init(b,c/2)}},j=b.Latin1={stringify:function(a){var c=a.words;a=a.sigBytes;for(var b=[],f=0;f<a;f++)b.push(String.fromCharCode(c[f>>>2]>>>24-8*(f%4)&255));return b.join("")},parse:function(a){for(var c=a.length,b=[],f=0;f<c;f++)b[f>>>2]|=(a.charCodeAt(f)&255)<<24-8*(f%4);return new p.init(b,c)}},h=b.Utf8={stringify:function(a){try{return decodeURIComponent(escape(j.stringify(a)))}catch(c){throw Error("Malformed UTF-8 data");}},parse:function(a){return j.parse(unescape(encodeURIComponent(a)))}},
    r=d.BufferedBlockAlgorithm=k.extend({reset:function(){this._data=new p.init;this._nDataBytes=0},_append:function(a){"string"==typeof a&&(a=h.parse(a));this._data.concat(a);this._nDataBytes+=a.sigBytes},_process:function(a){var c=this._data,b=c.words,f=c.sigBytes,d=this.blockSize,e=f/(4*d),e=a?g.ceil(e):g.max((e|0)-this._minBufferSize,0);a=e*d;f=g.min(4*a,f);if(a){for(var k=0;k<a;k+=d)this._doProcessBlock(b,k);k=b.splice(0,a);c.sigBytes-=f}return new p.init(k,f)},clone:function(){var a=k.clone.call(this);
    a._data=this._data.clone();return a},_minBufferSize:0});d.Hasher=r.extend({cfg:k.extend(),init:function(a){this.cfg=this.cfg.extend(a);this.reset()},reset:function(){r.reset.call(this);this._doReset()},update:function(a){this._append(a);this._process();return this},finalize:function(a){a&&this._append(a);return this._doFinalize()},blockSize:16,_createHelper:function(a){return function(b,d){return(new a.init(d)).finalize(b)}},_createHmacHelper:function(a){return function(b,d){return(new s.HMAC.init(a,
    d)).finalize(b)}}});var s=e.algo={};return e}(Math);
    (function(){var g=CryptoJS,l=g.lib,e=l.WordArray,d=l.Hasher,m=[],l=g.algo.SHA1=d.extend({_doReset:function(){this._hash=new e.init([1732584193,4023233417,2562383102,271733878,3285377520])},_doProcessBlock:function(d,e){for(var b=this._hash.words,n=b[0],j=b[1],h=b[2],g=b[3],l=b[4],a=0;80>a;a++){if(16>a)m[a]=d[e+a]|0;else{var c=m[a-3]^m[a-8]^m[a-14]^m[a-16];m[a]=c<<1|c>>>31}c=(n<<5|n>>>27)+l+m[a];c=20>a?c+((j&h|~j&g)+1518500249):40>a?c+((j^h^g)+1859775393):60>a?c+((j&h|j&g|h&g)-1894007588):c+((j^h^
    g)-899497514);l=g;g=h;h=j<<30|j>>>2;j=n;n=c}b[0]=b[0]+n|0;b[1]=b[1]+j|0;b[2]=b[2]+h|0;b[3]=b[3]+g|0;b[4]=b[4]+l|0},_doFinalize:function(){var d=this._data,e=d.words,b=8*this._nDataBytes,g=8*d.sigBytes;e[g>>>5]|=128<<24-g%32;e[(g+64>>>9<<4)+14]=Math.floor(b/4294967296);e[(g+64>>>9<<4)+15]=b;d.sigBytes=4*e.length;this._process();return this._hash},clone:function(){var e=d.clone.call(this);e._hash=this._hash.clone();return e}});g.SHA1=d._createHelper(l);g.HmacSHA1=d._createHmacHelper(l)})();
    (function(){var g=CryptoJS,l=g.enc.Utf8;g.algo.HMAC=g.lib.Base.extend({init:function(e,d){e=this._hasher=new e.init;"string"==typeof d&&(d=l.parse(d));var g=e.blockSize,k=4*g;d.sigBytes>k&&(d=e.finalize(d));d.clamp();for(var p=this._oKey=d.clone(),b=this._iKey=d.clone(),n=p.words,j=b.words,h=0;h<g;h++)n[h]^=1549556828,j[h]^=909522486;p.sigBytes=b.sigBytes=k;this.reset()},reset:function(){var e=this._hasher;e.reset();e.update(this._iKey)},update:function(e){this._hasher.update(e);return this},finalize:function(e){var d=
    this._hasher;e=d.finalize(e);d.reset();return d.finalize(this._oKey.clone().concat(e))}})})();
    /* eslint-enable */

    // Auth
    const devId = '<insert_here>'
    const apiKey = '<insert_here>'

    // Create signature
    let request = query
    if (!request.match(/\/.*\?/)) request += '?'
    request += '&devid=' + devId
    const signature = CryptoJS.HmacSHA1(request, apiKey);

    // Return built URL
    const url = 'https://timetableapi.ptv.vic.gov.au' + request + '&signature=' + signature.toString()
    return url
  }

  // Handling page interactivity
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    // Not logging so this is what we use
    var loggingBox = clayConfig.getItemById('search_status');
    loggingBox.set('Loaded!')
    loggingBox.hide()

    // Need to reset on search!
    const blankRadioListHTML = clayConfig.getItemById('search_radio').$element.select('div[class="radio-group"]').get("innerHTML")
    const resetSearch = function(){
      clayConfig.getItemById('search_text').set('')
      clayConfig.getItemById('search_radio').hide()
      clayConfig.getItemById('search_radio_select').hide()
      clayConfig.getItemById('search_set_to').hide()
      clayConfig.getItemById('search_radio').$element.select('div[class="radio-group"]').set("innerHTML", blankRadioListHTML)
    }
    resetSearch()

    // Attach a listener to search button
    clayConfig.getItemById('search_button').on('click', () => {
      var loggingBox = clayConfig.getItemById('search_status');
      loggingBox.set('Button clicked')

      // Perform a lookup
      var searchText = clayConfig.getItemById('search_text');
      const url = buildUrl('/v3/search/'+encodeURI(searchText.get().trim())+'?')
      loggingBox.set('Querying: ' + url)
      minified.$.request('get', url)
      .then(function(rsp){
        loggingBox.set('Done searching')
        var json = JSON.parse(rsp);
        loggingBox.set('Got result: ' + json.stops.length)

        // Always reset list on search (may skip select)
        clayConfig.getItemById('search_radio').$element.select('div[class="radio-group"]').set("innerHTML", blankRadioListHTML)

        // Parse first 5x results
        const stops = json.stops.map((stop) => {
          var info = {
            stop_name: stop.stop_name,
            route_type: stop.route_type,
            stop_id: stop.stop_id,
            description: '',
            routes: []
          }

          // Stop type
          var stopType = 'Unknown'
          if (stop.route_type === 0)
            stopType = 'Train'
          else if (stop.route_type === 1)
            stopType = 'Tram'
          else if (stop.route_type === 2)
            stopType = 'Bus'
          else if (stop.route_type === 3)
            stopType = 'V/Line'
          else if (stop.route_type === 4)
            stopType = 'Nightrider'
          
          // Add routes departing from stop
          if ([1, 2].includes(stop.route_type)){
            const routes = stop.routes.map((route) => route.route_number).slice(0, 3)
            info.routes = routes
          }

          // Provide stop description (including transport type and served routes)
          info.description += '[' + stopType + ']'
          info.description += ' ' + stop.stop_name
          if (info.routes.length){
            var routeInfo = info.routes.join(', ')
            if (info.routes.length != stop.routes.length) routeInfo += '...'
            info.description += ' (' + routeInfo + ')'
          }
          return info
        }).slice(0, 5)

        // Retrieve HTML with single blank item
        const radioList = clayConfig.getItemById('search_radio').$element.select('div[class="radio-group"]')
        const radioListHtml = radioList.get("innerHTML")
          
        // Build new HTML with option per result
        var newHtml = ''
        for (let i = 0; i < stops.length; i++){
          const selectionKey = [stops[i].route_type, stops[i].stop_id, encodeURI(stops[i].stop_name)].join('_')
          const html = radioListHtml.replace('...', stops[i].description).replace('route-type_stop-id_stop-name', selectionKey)
          newHtml += html
        }

        // Show search results
        radioList.set("innerHTML", newHtml)
        clayConfig.getItemById('search_radio').show()
        clayConfig.getItemById('search_radio_select').show()
        clayConfig.getItemById('search_set_to').show()
      })
      .error(function(status, _statusText, responseText) {
        loggingBox.set('Failed with status '+status+' and text: ' + responseText)
      })
    });

    // Attach listener to search select button
    clayConfig.getItemById('search_radio_select').on('click', () => {
      // Find selection
      const selectedSearch = clayConfig.getItemById('search_radio').get()
      const selectedBox = clayConfig.getItemById('search_set_to').get().slice(-1)
      if (!selectedSearch)
        return
      
      // Apply selection
      loggingBox.set("Selected to box " + selectedBox + ' :' + selectedSearch)
      var [route_type, stop_id, stop_name] = selectedSearch.split('_')
      clayConfig.getItemById('SETTINGS_STOP_' + selectedBox +'_NAME').set(decodeURI(stop_name))
      clayConfig.getItemById('SETTINGS_STOP_' + selectedBox +'_STOP').set(stop_id)
      clayConfig.getItemById('SETTINGS_STOP_' + selectedBox +'_TYPE').set(route_type)
      resetSearch()
    })
  });
}