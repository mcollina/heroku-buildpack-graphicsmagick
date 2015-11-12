Vagrant.configure("2") do |config|
  config.vm.define "cedar-14", primary: true do |config|
    config.vm.box = "puppetlabs/ubuntu-14.04-64-nocm"
    config.ssh.forward_agent = true

    config.vm.provider :vmware_fusion do |v|
      v.vmx["memsize"] = "512"
      v.vmx["numvcpus"] = "1"
    end

    config.vm.provision :shell, inline: <<-EOS
bash <(curl -s https://raw.githubusercontent.com/heroku/stack-images/master/bin/cedar-14.sh | sed 's/pruned_find -perm .* [|] xargs -r chmod .*//' | sed 's,chown root:root /var/lib/libuuid,,')
mkdir -p /app && chown vagrant:vagrant /app
EOS
  end
end
