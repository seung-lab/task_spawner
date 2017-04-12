import os
import multiprocessing
import cPickle as pickle

import worker

TMPDIR = "/tmp/"
WORKERS = 60

total_tasks = 0

def retrieve_tasks(dataset_id):
    if dataset_id == 11:
        bucket = "zfish"
    elif dataset_id == 1:
        bucket = "e2198_compressed"

    with open("{}{}.tasks".format(TMPDIR, bucket), "rb") as f:
        return bucket, pickle.load(f)


def worker_main(queue):
    global proc_tasks

    while True:
        task = queue.get(True)
        if task is None:
            queue.put(None) # Put it back for other running workers
            return
        else:
            print("Processing task {}/{}".format(task["id"], total_tasks))
            os.sys.stdout.flush()
            worker.calcSpawnTable(task["bucket"], task["pre"], task["post"])



def main():
    global total_tasks

    # Get Worker Tasks
    print("Loading tasks...")
    os.sys.stdout.flush()

    bucket, tasks = retrieve_tasks(11)
    total_tasks = len(tasks)

    print("Done. Found {} tasks".format(total_tasks))
    os.sys.stdout.flush()

    # Create Queue and Pool
    queue = multiprocessing.Queue()
    pool = multiprocessing.Pool(WORKERS, worker_main, (queue,))

    task_cnt = 0
    for center_path, neighbor_path in tasks:
        params = {
            "id": task_cnt,
            "bucket": bucket,
            "pre": center_path,
            "post": neighbor_path
        }
        queue.put(params)
        task_cnt += 1

    # Add poison pill to shutdown workers
    queue.put(None)

    # Clean up
    pool.close()
    pool.join()


if __name__ == '__main__':
    main()
