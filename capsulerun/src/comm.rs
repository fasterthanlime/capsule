use capnp::capability::Promise;
use capnp::Error;
use capnp_rpc::pry;
use futures::Future;
use proto::proto_capnp::host;

use log::*;
use std::fmt;
use std::fs::File;
use std::io::Write;
use std::sync::{Arc, Mutex};

struct SinkImpl {
  session: Arc<Mutex<Option<host::session::Client>>>,
  skip: u32,
  count: u32,
  file: File,
}

impl host::sink::Server for SinkImpl {
  fn send_video_frame(
    &mut self,
    params: host::sink::SendVideoFrameParams,
    _results: host::sink::SendVideoFrameResults,
  ) -> Promise<(), Error> {
    if self.skip > 0 {
      self.skip -= 1;
      info!("Skipping ({} left)", self.skip);
      return Promise::ok(());
    }

    let params = pry!(params.get());
    let frame = pry!(params.get_frame());
    let index = frame.get_index();
    let data = pry!(frame.get_data());
    let millis = pry!(frame.get_timestamp()).get_millis();
    let timestamp = std::time::Duration::from_millis(millis as u64);

    let mut num_black = 0;
    let num_pixels = data.len() / 4;
    for i in 0..num_pixels {
      let (r, g, b) = (data[i * 4], data[i * 4 + 1], data[i * 4 + 2]);
      if r == 0 && g == 0 && b == 0 {
        num_black += 1;
      }
    }

    info!(
      "Received frame {} (@ {:?}), contains {} bytes, {}/{} black pixels",
      index,
      timestamp,
      data.len(),
      num_black,
      num_pixels
    );
    self.file.write(data).unwrap();

    self.count += 1;
    if self.count > 60 * 4 {
      let guard = self.session.lock().unwrap();
      if let Some(session) = guard.as_ref() {
        let req = session.stop_request();
        return Promise::from_future(req.send().promise.and_then(|_| {
          info!("Stopped capture session!");
          Promise::ok(())
        }));
      }
    }
    Promise::ok(())
  }
}

struct Target {
  client: host::target::Client,
  pid: u64,
  exe: String,
}

impl fmt::Display for Target {
  fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
    write!(f, "{}:{}", self.pid, self.exe)
  }
}

pub struct HostImpl {
  target: Option<Target>,
}

impl HostImpl {
  pub fn new() -> Self {
    HostImpl { target: None }
  }
}

impl host::Server for HostImpl {
  fn register_target(
    &mut self,
    params: host::RegisterTargetParams,
    mut _results: host::RegisterTargetResults,
  ) -> Promise<(), Error> {
    let params = pry!(params.get());
    let client = pry!(params.get_target());
    let info = pry!(params.get_info());
    {
      let pid = info.get_pid();
      let exe = info.get_exe().unwrap();
      let target = Target {
        pid: pid,
        exe: exe.to_owned(),
        client: client.clone(),
      };
      info!("Target registered: {}", target);
      self.target = Some(target);
    }

    {
      info!("Asking to start capture...");
      // creating a sink here, but it has a "None" session
      let sink = SinkImpl {
        session: Arc::new(Mutex::new(None)),
        skip: 300,
        count: 0,
        file: File::create("capture.raw").unwrap(),
      };
      let session_ref = sink.session.clone();
      let mut req = client.start_capture_request();
      // moving the sink into an RPC client thingy
      req
        .get()
        .set_sink(host::sink::ToClient::new(sink).into_client::<::capnp_rpc::Server>());
      Promise::from_future(req.send().promise.and_then(move |res| {
        info!("Started capture succesfully!");
        let res = pry!(res.get());
        let session = pry!(res.get_session());
        // need to mutate the interior of the sink here, Arc/Mutex to the rescue
        *session_ref.lock().unwrap() = Some(session);
        Promise::ok(())
      }))
    }
  }
}
