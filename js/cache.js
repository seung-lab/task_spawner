let lz4        = require('lz4');             // (de)compression of segmentation from/to redis

/* cachedFetch

 * Input: request-promise input, e.g. { url: path, encoding: null }
 * 
 * Description: Checks if the requested object exists in cache (using path as key).
 *              If not, download it and send it gzipped to cache, otherwise retrieve and decompress it from cache.
 *              LZMA segmentation (file ends with .lzma) will be decompressed first before it is recompressed with
 *              gzip and send to cache (trading a slight increase in file size for major speedup).
 * 
 * Returns: Buffer
 */
function cachedFetch(request) {
    return redis.getAsync(request.url)
        .then(function (value) {
            if (value !== null) {
                let decoded_resp = lz4.decode(value);
                console.log(request.url + " successfully retrieved from cache.");
                return decoded_resp;
            }
            else
            {
                console.log(request.url + " not in cache. Downloading ...");
                return rp(request)
                .then(function (resp) {
                    console.log(request.url + " successfully downloaded.");
                    if (request.url.endsWith(".lzma")) {
                        console.log("Decompressing " + request.url);
                        return lzma.decompress(resp);
                    }
                    else {
                        return resp;
                    }
                })
                .then(function (decoded_resp) {
                    compressed_resp = lz4.encode(decoded_resp);
                    console.log(request.url + " compressed (Ratio: " + (100.0 * compressed_resp.byteLength / decoded_resp.byteLength).toFixed(2) + " %)");
                    return redis.setAsync(request.url, compressed_resp)
                    .then(function () {
                        console.log(request.url + " sent to cache.");
                        return decoded_resp;
                    })
                    .catch(function (err) {
                        console.log("Caching " + request.url + " failed: " + err);
                        return decoded_resp;
                    });
                })
                .catch(function (err) {
                    throw new Error("Aquiring " + request.url + " failed: " + err);
                });
            }
        })
        .catch(function (err) {
            throw new Error("Unknown redis error when loading " + request.url + ": " + err);
        });
}