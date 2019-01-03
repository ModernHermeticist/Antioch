using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Pointer : MonoBehaviour
{
    private GameObject player;

    private Vector3 pointerOffset = new Vector3(0, 0, 0);
    private Vector3 mousePos;
    private Vector3 playerPos = new Vector3(0,0,0);
    private Vector3 rotationVector;
    private float angle;

	// Use this for initialization
	void Start ()
    {
        player = transform.parent.gameObject;
        rotationVector = player.transform.rotation.eulerAngles;
	}
	
	// Update is called once per frame
	void Update ()
    {
        playerPos = player.transform.position;
        mousePos = Input.mousePosition; // get mouse position
        mousePos = Camera.main.ScreenToWorldPoint(mousePos);
        //mousePos.x = Mathf.Clamp(mousePos.x, -1, 1);
        //mousePos.y = Mathf.Clamp(mousePos.y, -1, 1);

        angle = Mathf.Atan2(mousePos.y - playerPos.y, mousePos.x - playerPos.x) * 180 / Mathf.PI;
        rotationVector.z = angle;
        angle = angle / 180 * Mathf.PI; // Convert angle for moving pointer with mouse

        pointerOffset.x = Mathf.Cos(angle) * 0.16f;
        pointerOffset.y = Mathf.Sin(angle) * 0.16f;
        //pointerOffset = pointerOffset.normalized * 0.16f;

        transform.position = player.transform.position;
        transform.position += pointerOffset;
        transform.rotation = Quaternion.Euler(rotationVector);
	}

    private void OnTriggerEnter2D(Collider2D collision)
    {
        if (collision.gameObject.tag == "Enemy")
        {
            player.GetComponent<Player>().target = collision.gameObject;
            player.GetComponent<Player>().canAttack = true;
        }
    }

    private void OnTriggerExit2D(Collider2D collision)
    {
        if (player.GetComponent<Player>().canAttack == true)
        {
            player.GetComponent<Player>().target = null;
            player.GetComponent<Player>().canAttack = false;
        }
    }
}
