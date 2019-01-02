using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Pointer : MonoBehaviour
{
    private GameObject player;
    public new Camera camera;

    private Vector3 pointerOffset = new Vector3(0, 0, 0);
    private Vector3 mousePos;
    private Vector3 playerPos;
    private Vector3 rotationVector;
    private float mousePosY, mousePosX;
    private float angle;

	// Use this for initialization
	void Start ()
    {
        player = GameObject.Find("Player");
        playerPos = player.transform.position;
        rotationVector = player.transform.rotation.eulerAngles;
	}
	
	// Update is called once per frame
	void Update ()
    {
        mousePos = Input.mousePosition; // get mouse position
        mousePos = camera.ScreenToWorldPoint(mousePos);
        mousePos.x = Mathf.Clamp(mousePos.x, -1, 1);
        mousePos.y = Mathf.Clamp(mousePos.y, -1, 1);


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

    private void OnCollisionEnter2D(Collision2D collision)
    {
        player.c
        playerScript.target = collision.gameObject;
    }

    private void OnCollisionExit2D(Collision2D collision)
    {
        playerScript.canAttack = false;
        playerScript.target = null;
    }
}
