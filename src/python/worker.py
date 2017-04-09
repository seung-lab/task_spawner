import requests
import pylzma
import httplib # catching httplib exception
import os
import logging
import multiprocessing
from retrying import retry
from google.cloud import storage
from ctypes import *

lib = cdll.LoadLibrary("../../lib/spawnsetgenerator.so")
storage_client = storage.Client()

class InputVolume(Structure):
    _fields_ = [("metadata", c_char_p),
                ("bboxesLength", c_uint),
                ("bboxes", c_void_p),
                ("sizesLength", c_uint),
                ("sizes", c_void_p),
                ("segmentationLength", c_uint),
                ("segmentation", c_void_p)]

class SpawnTableWrapper(Structure):
    _fields_ = [("spawntableLength", c_uint),
                ("spawntableBuffer", c_void_p)]

PSpawnTableWrapper = POINTER(SpawnTableWrapper)

TMPDIR = "/tmp/"

locks = {}
logging.basicConfig(filename='spawn.log',level=logging.DEBUG)

def retry_if_backend_error(exception):
    if isinstance(exception, httplib.InternalServerError):
        return True
    elif isinstance(exception, httplib.IncompleteRead):
        return True
    return False

@retry(retry_on_exception=retry_if_backend_error, wait_exponential_multiplier=1000, wait_exponential_max=10000)
def download_file(url):
    response = requests.get(url)
    return response.content

@retry(stop_max_attempt_number=3, wait_fixed=2000)
def unlzma(data):
    data = data[0:5] + data[13:]      #pylzma ignores 8 byte for length starting at 5th byte
    return pylzma.decompress(data)

def retrieve_file(bucket, path, filename):
    tmpdirname = "{}{}{}".format(TMPDIR, path, filename)
    (basename, ext) = os.path.splitext(filename)

    if tmpdirname not in locks:
        locks[tmpdirname] = multiprocessing.Lock()

    if os.path.isfile(tmpdirname):
        locks[tmpdirname].acquire()
        with open(tmpdirname, mode="rb") as f:
            response = f.read()
            locks[tmpdirname].release()
            if ext == ".lzma":
                response = unlzma(response)
            return response

    response = download_file("https://storage.googleapis.com/{}/{}{}".format(bucket, path, filename))
    locks[tmpdirname].acquire()
    if not os.path.exists("{}{}".format(TMPDIR, path)):
        os.makedirs("{}{}".format(TMPDIR, path))

    with open(tmpdirname, mode="wb") as f:
        f.write(response)
    locks[tmpdirname].release()

    if ext == ".lzma":
        response = unlzma(response)

    return response

def calcSpawnTable(bucket, pre_path, post_path):
    try:
        post_chunk = os.path.basename(os.path.normpath(post_path))
        #if requests.head("https://storage.googleapis.com/{}/{}{}.pb.spawn".format(bucket, pre_path, post_chunk)).status_code == 200:
        #    print("Skipping {}{}.pb.spawn".format(pre_path, post_chunk))
        #    return

        #print("Writing {}{}.pb.spawn".format(pre_path, post_chunk))

        pre_meta = retrieve_file(bucket, pre_path, "metadata.json")
        pre_seg = retrieve_file(bucket, pre_path, "segmentation.lzma")
        pre_sizes = retrieve_file(bucket, pre_path, "segmentation.size")
        pre_boxes = retrieve_file(bucket, pre_path, "segmentation.bbox")

        post_meta = retrieve_file(bucket, post_path, "metadata.json")
        post_seg = retrieve_file(bucket, post_path, "segmentation.lzma")
        post_sizes = retrieve_file(bucket, post_path, "segmentation.size")
        post_boxes = retrieve_file(bucket, post_path, "segmentation.bbox")

        pre_seg_len = len(pre_seg)
        pre_sizes_len = len(pre_sizes)
        pre_boxes_len = len(pre_boxes)
        post_seg_len = len(post_seg)
        post_sizes_len = len(post_sizes)
        post_boxes_len = len(post_boxes)

        pre_volume  = InputVolume(c_char_p(pre_meta), pre_boxes_len, cast(c_char_p(pre_boxes), c_void_p), pre_sizes_len, cast(c_char_p(pre_sizes), c_void_p), pre_seg_len, cast(c_char_p(pre_seg), c_void_p))
        post_volume  = InputVolume(c_char_p(post_meta), post_boxes_len, cast(c_char_p(post_boxes), c_void_p), post_sizes_len, cast(c_char_p(post_sizes), c_void_p), post_seg_len, cast(c_char_p(post_seg), c_void_p))

        result_p = cast(lib.SpawnSet_Generate(pointer(pre_volume), pointer(post_volume)), PSpawnTableWrapper)

        buffer = (c_char * result_p.contents.spawntableLength).from_address(result_p.contents.spawntableBuffer)

        gcloud_bucket = storage.bucket.Bucket(storage_client, bucket)
        gcloud_blob = storage.blob.Blob("{}{}.pb.spawn".format(pre_path, post_chunk), gcloud_bucket)
        gcloud_blob.upload_from_string(buffer, content_type="application/octet-stream", client=storage_client)

        lib.SpawnSet_Release(result_p)
    except Exception:
        logging.error("{}{}.pb.spawn".format(pre_path, post_chunk), exc_info=True)
